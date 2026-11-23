/* 启动内存盘后端实现，避免把设备调用与数据传输流程展开在头文件。 */
#include "service/device/storage/boot_image.h"

namespace storage {

/* 查询内核保留的启动映像，不存在时不发布该后端。 */
bool BootImageDriver::init() {
    os::IpcMessage request{}, reply{};
    request.type =
        static_cast<uint64_t>(services::DeviceOperation::BootImageInfo);
    const int64_t result =
        os::detail::RequestChannel(services::CoreServiceKind::Device)
            .result(request, reply);
    blocks_ = result > 0 ? result : 0;
    online_ = blocks_ != 0;
    return online_;
}

/* 持锁取得容量和在线状态的一致快照。 */
BlockInfo BootImageDriver::info() const {
    os::Mutex::Guard guard(lock_);
    return infoLocked();
}

/* 读取启动映像中的逻辑块，数据仍由内核设备接口提供。 */
TransferResult BootImageDriver::read(uint64_t first, uint32_t count, void* buffer,
                        size_t bytes) {
    return transfer(first, count, buffer, nullptr, bytes, false);
}

/* 写入内存中的映像副本，不把它当成持久化磁盘。 */
TransferResult BootImageDriver::write(uint64_t first, uint32_t count, const void* buffer,
                         size_t bytes) {
    return transfer(first, count, nullptr, buffer, bytes, true);
}

/* 内存拷贝在锁释放前已经完成，内存盘无需发送硬件刷新命令。 */
os::Storage::Error BootImageDriver::flush() {
    os::Mutex::Guard guard(lock_);
    // transfer 与这里共用锁，取得锁时之前的内核拷贝已完成。
    return online_ ? os::Storage::Error::Success
                   : os::Storage::Error::DeviceOffline;
}

/* 停用后拒绝后续读写，不释放仍由内核管理的启动模块。 */
StopResult BootImageDriver::stop() {
    os::Mutex::Guard guard(lock_);
    online_ = false;
    return StopResult::Stopped;
}

/* 调用者已持锁时生成能力记录，明确标记为易失内存。 */
BlockInfo BootImageDriver::infoLocked() const {
    return {blocks_,
            logicalBlockSize,
            services::maximumStorageTransferBlocks,
            online_,
            false,
            CacheMode::VolatileMemory,
            false};
}

/* 共用范围检查与消息编码，确认返回字节数正确后才向调用者报告成功。 */
TransferResult BootImageDriver::transfer(uint64_t block, uint32_t count, void* output,
                            const void* input, size_t capacity, bool write) {
    using Error = os::Storage::Error;
    os::Mutex::Guard guard(lock_);
    const Error error =
        infoLocked().validateTransfer(block, count, write ? input : output,
                         capacity, write);
    if (error != Error::Success) return {error, 0};

    const uint32_t bytes = count * logicalBlockSize;
    const uint32_t words = count * wordsPerBlock;
    os::IpcMessage request{}, reply{};
    request.type =
        static_cast<uint64_t>(write ? services::DeviceOperation::BootImageWrite
                                    : services::DeviceOperation::BootImageRead);
    request.data[0] = block;
    request.data[1] = count;
    request.length  = write ? 2 + words : 2;
    if (write) os::Runtime::copyBytes(&request.data[2], input, bytes);
    const int64_t status =
        os::detail::RequestChannel(services::CoreServiceKind::Device)
            .result(request, reply);
    if (status) return {transportError(status), 0};
    if (reply.length < (write ? 2 : 2 + words) || reply.data[1] != bytes)
        return {Error::Io, 0};
    if (!write) os::Runtime::copyBytes(output, &reply.data[2], bytes);
    return {Error::Success, count};
}

}  // namespace storage
