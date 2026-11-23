/*
 * 只对带有专用标记的测试盘做破坏性读写；系统盘仅通过文件系统创建临时文件。
 */
#include <fcntl.h>
#include <os/detail/client.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/storage.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "service/common/protocol.h"

static uint32_t scratchId;
int filesystemPermissions();
static const uint32_t blocksPerRequest = 7;

/* 把失败位置写到串口，即使窗口或终端没有启动也能保留证据。 */
static int failed(int line) {
    char message[96];
    snprintf(message, sizeof(message), "[STORAGE-TEST] FAIL line=%d\n", line);
    os::Sys().log(message);
    return line;
}

#define CHECK(expression)            \
    do {                             \
        if (!(expression))           \
            return failed(__LINE__); \
    } while (0)

/* 连续范围检查合并，间隔范围检查批量在途；刷新请求排在七条写之后。 */
static int asynchronousBatch() {
    os::Ipc completion;
    CHECK(completion.create() == 0);
    os::Ipc::Handle service(
        os::detail::RegistryClient().lookup(services::storageServiceName));
    CHECK(service.getValue() != 0);
    for (uint32_t stride = 1; stride <= 2; ++stride) {
        for (uint32_t read = 0; read < 2; ++read) {
            for (uint32_t index = 0; index < 7; ++index) {
                os::IpcMessage request{};
                request.type = static_cast<uint64_t>(
                    read ? services::StorageRequest::ReadAsync
                         : services::StorageRequest::WriteAsync);
                request.data[0] =
                    (uint64_t(scratchId) << services::storageDeviceShift) |
                    (256 + index * stride);
                request.data[1] = index;
                request.data[2] = 512;
                request.length = read ? 2 : os::ipcDataWordCount;
                request.handleCount = 1;
                request.handles[0] = completion.getHandle();
                request.handleOptions[0] = services::ipcHandleRestrictRights;
                request.handleRights[0] = 2;
                for (uint32_t byte = 0; byte < 512 && !read; ++byte) {
                    reinterpret_cast<uint8_t*>(&request.data[3])[byte] =
                        static_cast<uint8_t>(index * 37 + stride + byte);
                }

                CHECK(os::sendMessage(service.getValue(), request) == 0);
            }

            if (!read) {
                os::Storage::Device disk;
                CHECK(disk.open(scratchId) == 0 &&
                      disk.flushStatus() == os::Storage::Error::Success);
            }

            bool seen[7]{};
            for (uint32_t count = 0; count < 7; ++count) {
                os::IpcMessage response{};
                CHECK(os::receiveMessage(
                          completion.getHandle(), response,
                          os::Sys().readMonotonicTime() + 5000000000ULL) == 0);
                CHECK(response.type ==
                          services::storageCompletionNotification &&
                      response.length == (read ? 67U : 3U) &&
                      response.data[0] < 7 && response.data[1] == 0 &&
                      response.data[2] == 512);
                const uint32_t index = response.data[0];
                CHECK(!seen[index]);
                seen[index] = true;
                for (uint32_t byte = 0; byte < 512 && read; ++byte) {
                    CHECK(reinterpret_cast<uint8_t*>(&response.data[3])[byte] ==
                          static_cast<uint8_t>(index * 37 + stride + byte));
                }
            }
        }
    }

    return 0;
}

/* 每个线程使用独立客户端和磁盘范围，检查队列并发时是否串包或返回陈旧数据。 */
static void* worker(void* argument) {
    const uintptr_t number = reinterpret_cast<uintptr_t>(argument);
    os::Storage::Device disk;
    uint8_t written[blocksPerRequest * 512];
    uint8_t actual[sizeof(written)];
    if (disk.open(scratchId)) {
        return reinterpret_cast<void*>(1);
    }

    for (uint32_t round = 0; round < 24; ++round) {
        for (size_t index = 0; index < sizeof(written); ++index) {
            written[index] =
                static_cast<uint8_t>(number * 37 + round * 11 + index);
        }

        uint32_t completed = 0;
        const uint64_t first = 64 + number * 16;
        if (disk.write(first, blocksPerRequest, written, sizeof(written),
                       completed) ||
            completed != blocksPerRequest ||
            disk.read(first, blocksPerRequest, actual, sizeof(actual),
                      completed) ||
            completed != blocksPerRequest ||
            memcmp(written, actual, sizeof(actual))) {
            return reinterpret_cast<void*>(2);
        }
    }

    return nullptr;
}

/* 覆盖设备发现、批量传输、越界拒绝、只读盘、统计、并发队列以及文件系统落盘。 */
static int exercise() {
    using Error = os::Storage::Error;
    os::Storage inventory;
    CHECK(inventory.refresh() == 0);
    bool foundScratch = false;
    bool foundReadOnly = false;
    static os::Storage::Device disk;
    static os::Storage::Device readOnly;
    static uint8_t bytes[16 * 512];
    static uint8_t actual[sizeof(bytes)];

    for (uint32_t index = 0; index < inventory.getDeviceCount(); ++index) {
        os::Storage::Device candidate;
        CHECK(candidate.open(index) == 0);
        if (candidate.getPartitionNumber() || candidate.getBlockSize() != 512) {
            continue;
        }

        uint32_t completed = 0;
        if (candidate.getBlockCount() == 32768 && !candidate.getReadOnly()) {
            CHECK(candidate.read(0, 1, actual, sizeof(actual), completed) == 0);
            CHECK(completed == 1 &&
                  !memcmp(actual, "OSINFO-STORAGE-SCRATCH", 22));
            scratchId = index;
            CHECK(disk.open(index) == 0);
            foundScratch = true;
        }

        if (candidate.getBlockCount() == 24576 && candidate.getReadOnly()) {
            CHECK(candidate.read(0, 1, actual, sizeof(actual), completed) == 0);
            CHECK(completed == 1 &&
                  !memcmp(actual, "OSINFO-STORAGE-READONLY", 23));
            CHECK(readOnly.open(index) == 0);
            foundReadOnly = true;
        }
    }

    CHECK(foundScratch && foundReadOnly);
    CHECK(disk.getMaxTransferBlocks() > 0);

    os::Storage::Statistics before, after;
    CHECK(disk.statistics(before) == Error::Success);
    for (size_t index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = static_cast<uint8_t>(index * 13 + 7);
    }

    uint32_t completed = 0;
    CHECK(disk.write(32, 16, bytes, sizeof(bytes), completed) == 0 &&
          completed == 16);
    CHECK(disk.flushStatus() == Error::Success);
    CHECK(disk.read(32, 16, actual, sizeof(actual), completed) == 0 &&
          completed == 16);
    CHECK(!memcmp(bytes, actual, sizeof(bytes)));
    CHECK(disk.read(disk.getBlockCount(), 1, actual, sizeof(actual),
                    completed) == static_cast<int>(Error::InvalidBlock) &&
          completed == 0);
    CHECK(disk.read(0, 2, actual, 512, completed) ==
              static_cast<int>(Error::InvalidArgument) &&
          completed == 0);
    CHECK(readOnly.write(32, 1, bytes, sizeof(bytes), completed) ==
              static_cast<int>(Error::ReadOnly) &&
          completed == 0);

    pthread_t threads[4];
    for (uintptr_t index = 0; index < 4; ++index) {
        CHECK(pthread_create(&threads[index], nullptr, worker,
                             reinterpret_cast<void*>(index)) == 0);
    }

    for (size_t index = 0; index < 4; ++index) {
        void* result = nullptr;
        CHECK(pthread_join(threads[index], &result) == 0 && result == nullptr);
    }

    CHECK(disk.flushStatus() == Error::Success);
    CHECK(disk.statistics(after) == Error::Success);
    CHECK(after.completedRequests > before.completedRequests);
    CHECK(after.readBytes >= before.readBytes + sizeof(bytes));
    CHECK(after.writtenBytes >= before.writtenBytes + sizeof(bytes));
    CHECK(after.errors == before.errors && after.timeouts == before.timeouts);
    CHECK(after.inFlight == 0);
    CHECK(asynchronousBatch() == 0);

    int file = open("/tmp/storage-smoke.bin", O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(file >= 0);
    CHECK(write(file, bytes, sizeof(bytes)) ==
          static_cast<ssize_t>(sizeof(bytes)));
    CHECK(fsync(file) == 0 && close(file) == 0);
    file = open("/tmp/storage-smoke.bin", O_RDONLY);
    CHECK(file >= 0);
    CHECK(read(file, actual, sizeof(actual)) ==
          static_cast<ssize_t>(sizeof(actual)));
    CHECK(!memcmp(bytes, actual, sizeof(bytes)));
    CHECK(close(file) == 0 && unlink("/tmp/storage-smoke.bin") == 0);
    CHECK(filesystemPermissions() == 0);
    sync();
    return 0;
}

/* 专用镜像把登录入口改成该程序；只允许 tty1 执行一次，其他会话留在等待状态。 */
int main(int, char**) {
    if (os::Runtime::current().getConsoleId() != 0) {
        for (;;) {
            os::sleep(1000);
        }
    }

    os::Sys().log("[STORAGE-TEST] BEGIN\n");
    const int result = exercise();
    os::Sys().log(result ? "[STORAGE-TEST] RESULT FAIL\n"
                         : "[STORAGE-TEST] RESULT PASS\n");
    for (;;) {
        os::sleep(1000);
    }
}
