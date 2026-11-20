// Baseline probe for a disposable OSInfo image; never run against a user disk.
#include <os/service.h>
#include <os/storage.h>
#include <os/task.h>
#include <fcntl.h>
#include <cstring>
#include <initializer_list>
#include <unistd.h>
#include "service/common/queries.h"
#include "service/common/requests.h"

namespace {
constexpr const char* path = "/m0-storage-baseline.bin";
constexpr size_t length = 12293;
unsigned char expected[length], actual[length];

bool check(bool value, const char* error) {
    if (!value) {
        os::Sys().log("[M0 STORAGE] FAIL ");
        os::Sys().log(error);
        os::Sys().log("\n");
    }
    return value;
}

bool checkRelease() {
#ifdef OSINFO_EXPECTED_RELEASE
    constexpr const char expected[] = OSINFO_EXPECTED_RELEASE "\n";
    char value[sizeof(expected)]{};
    for (const auto* path : {"/lib/version.1", "/sys/kernel/osrelease"}) {
        const int fd = open(path, O_RDONLY);
        if (!check(fd >= 0, "open release metadata")) return false;
        const auto size = read(fd, value, sizeof(value));
        if (!check(close(fd) == 0 && size == sizeof(expected) - 1 &&
                       std::memcmp(value, expected, sizeof(expected) - 1) == 0,
                   "release metadata mismatch"))
            return false;
    }
    os::Sys().log("[RELEASE] " OSINFO_EXPECTED_RELEASE " verified\n");
#endif
    return true;
}

bool fullCompletionQueue(uint32_t deviceId) {
    os::Ipc completion;
    if (!check(completion.create() == 0, "async completion endpoint")) return false;
    os::IpcMessage filler{};
    filler.type = 1;
    int status = 0;
    for (unsigned i = 0; i < 32 && status == 0; ++i)
        status = os::sendMessage(completion.getHandle(), filler, 0);
    if (!check(status == static_cast<int>(os::ErrorCode::WouldBlock),
               "async completion endpoint is full"))
        return false;
    os::Ipc::Handle service(os::detail::RegistryClient().lookup("storage"));
    if (!check(service.getValid(), "storage endpoint for async probe")) return false;
    const auto deadline = os::StorageProtocol::ipcDeadlineAfter(
        os::Sys().readMonotonicTime(), 5000000000ULL);
    for (unsigned i = 0; i < 6; ++i) {
        os::IpcMessage request{};
        request.type = static_cast<uint64_t>(os::StorageRequest::ReadAsync);
        request.length = 2;
        request.data[0] = os::StorageProtocol::encodeStorageBlock(deviceId, 0);
        request.data[1] = i + 1;
        request.handleCount = 1;
        request.handles[0] = completion.getHandle();
        if (!check(os::sendMessage(service.getValue(), request, deadline) == 0,
                   "submit async read with full completion endpoint"))
            return false;
    }
    os::IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(os::StorageRequest::GetCount);
    return check(os::callService(service.getValue(), request, reply, deadline) == 0 &&
                     reply.length >= 2 && reply.data[0] == 0,
                 "full completion endpoint cannot exhaust storage workers");
}

bool checkStorage() {
    os::Storage storage;
    if (!check(storage.refresh() == 0 && storage.getDeviceCount(), "enumeration"))
        return false;
    bool medium = false;
    uint32_t usableDevice = 0;
    unsigned char block[4096];
    for (uint32_t id = 0; id < storage.getDeviceCount(); ++id) {
        os::Storage::Device device;
        if (!check(device.open(id) == 0, "open device")) return false;
        if (device.getBlockSize() == 512 && device.getBlockCount() == 32768) {
            os::Storage::Statistics before{}, after{};
            if (!check(device.statistics(before) == os::Storage::Error::Success,
                       "scratch disk statistics before timeout"))
                return false;
            os::Sys().log("[M6 STORAGE] scratch ready for timeout\n");
            sleep(2);
            uint32_t done = 99;
            if (!check(device.read(0, 1, block, sizeof(block), done) ==
                           static_cast<int>(os::Storage::Error::Timeout) &&
                           done == 0 && device.statistics(after) ==
                               os::Storage::Error::Success &&
                           after.completedRequests == before.completedRequests + 1 &&
                           after.errors == before.errors + 1 &&
                           after.timeouts == before.timeouts + 1 &&
                           after.readBytes == before.readBytes && !after.inFlight,
                       "submitted timeout counted once without bytes"))
                return false;
            os::Sys().log("[M6 STORAGE] timeout statistics passed\n");
            if (!check(device.read(0, 1, block, sizeof(block), done) ==
                           static_cast<int>(os::Storage::Error::DeviceOffline) &&
                           done == 0,
                       "timed-out scratch disk stays offline"))
                return false;
            os::Sys().log("[M5 STORAGE] scratch disk offline\n");
            continue;
        }
        if (device.getBlockSize() == 4096) {
            uint32_t done = 99;
            if (!check(device.getBlockCount() == 4096 &&
                           device.getMaxTransferBlocks() == 0 &&
                           device.read(0, 1, block, sizeof(block), done) ==
                               static_cast<int>(os::Storage::Error::NotSupported) &&
                           done == 0,
                       "4Kn geometry and I/O rejection"))
                return false;
            os::Sys().log("[M0 STORAGE] 4Kn identified, I/O rejected\n");
            continue;
        }
        if (!check(device.getBlockSize() == 512 && device.getBlockCount() &&
                   device.getMaxTransferBlocks(), "device info")) return false;
        uint32_t done = 0;
        const uint32_t count = device.getBlockCount() < 7 ? 1 : 7;
        os::Storage::Device parent;
        os::Storage::Device* totals = &device;
        if (device.getPartitionNumber()) {
            if (!check(parent.open(device.getParentId()) == 0, "parent device"))
                return false;
            totals = &parent;
        }
        os::Storage::Statistics before{}, after{};
        if (!check(device.statistics(before) == os::Storage::Error::Success,
                   "initial statistics"))
            return false;
        if (!check(device.read(0, count, block, sizeof(block), done) == 0 &&
                   done == count, "read and completed count")) return false;
        if (!check(totals->statistics(after) == os::Storage::Error::Success &&
                       after.completedRequests > before.completedRequests &&
                       after.readBytes >= before.readBytes + count * 512,
                   "read statistics shared with parent"))
            return false;
        if (!check(device.read(device.getBlockCount() - 1, 1, block,
                               sizeof(block), done) == 0 && done == 1,
                   "last block is readable"))
            return false;
        done = 99;
        if (!check(device.read(device.getBlockCount(), 1, block, sizeof(block), done) != 0 &&
                   done == 0, "capacity boundary rejection")) return false;
        if (!check(device.flushStatus() == os::Storage::Error::Success &&
                       device.flush(), "detailed and legacy flush request"))
            return false;
        if (!device.getPartitionNumber()) {
            if (device.getType() == os::Storage::Device::Type::VirtioBlock) {
                os::Sys().log("[M0 STORAGE] BACKEND VirtioBlock\n");
                medium = true;
                usableDevice = id;
            } else if (device.getType() == os::Storage::Device::Type::BootImage) {
                os::Sys().log("[M0 STORAGE] BACKEND BootImage\n");
                medium = true;
                usableDevice = id;
            }
        }
    }
    if (!check(medium, "expected backend absent") || !fullCompletionQueue(usableDevice))
        return false;
    os::Sys().log("[M5 STORAGE] full completion queue passed\n");
    return true;
}

bool filesystem() {
    for (size_t i = 0; i < length; ++i) expected[i] = (i * 37 + i / 511) & 255;
    int descriptor = open(path, O_RDONLY);
    const bool existed = descriptor >= 0;
    if (!existed) {
        descriptor = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (!check(descriptor >= 0, "create root FS file")) return false;
        size_t offset = 0;
        while (offset < length) {
            const ssize_t count = write(descriptor, expected + offset, length - offset);
            if (!check(count > 0, "FS write")) return false;
            offset += count;
        }
        if (!check(fsync(descriptor) == 0, "FS fsync")) return false;
        if (!check(close(descriptor) == 0, "FS close after write")) return false;
        descriptor = open(path, O_RDONLY);
        if (!check(descriptor >= 0, "FS reopen")) return false;
    }
    size_t offset = 0;
    while (offset < length) {
        const ssize_t count = read(descriptor, actual + offset, length - offset);
        if (!check(count > 0, "FS read")) return false;
        offset += count;
    }
    unsigned char extra;
    const bool correct = read(descriptor, &extra, 1) == 0 &&
                         std::memcmp(expected, actual, length) == 0;
    if (!check(close(descriptor) == 0 && correct, "FS length and content")) return false;
    os::Sys().log(existed ? "[M0 STORAGE] FILE persisted\n" : "[M0 STORAGE] FILE created\n");
    return true;
}
}  // namespace

int main() {
    // The image's session manager launches one probe per text terminal.
    // Runtime initialization consumes the reserved terminal argument.
    os::Task task;
    if (task.openCurrent() == 0 && task.getTerminalKind() == 1 &&
        task.getTerminalIndex() == 0) {
        os::Sys().log("[M0 STORAGE] START\n");
        if (checkRelease() && checkStorage() && filesystem())
            os::Sys().log("[M0 STORAGE] PASS\n");
    }
    for (;;) sleep(1);
}
