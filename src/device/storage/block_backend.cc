/* 块设备能力相关判断由 BlockInfo 自身负责，驱动只提供真实能力值。 */
#include "service/device/storage/block_backend.h"

namespace storage {

    /* 容量乘法可能溢出，几何无效时返回零。 */
    uint64_t BlockInfo::capacityBytes() const {
        if (!logicalBlockSize_ ||
            blockCount_ > UINT64_MAX / logicalBlockSize_) {
            return 0;
        }

        return blockCount_ * logicalBlockSize_;
    }

    /* 所有后端共用缓冲区、块范围和只读检查，避免各驱动对同一请求给出不同结果。
     */
    os::Storage::Error BlockInfo::validateTransfer(uint64_t first,
                                                   uint32_t count,
                                                   const void* buffer,
                                                   size_t bytes,
                                                   bool write) const {
        using Error = os::Storage::Error;
        if (!online_) {
            return Error::DeviceOffline;
        }

        if (!capacityBytes() || !maxTransferBlocks_ || !buffer || !count ||
            count > maxTransferBlocks_ || count > bytes / logicalBlockSize_) {
            return Error::InvalidArgument;
        }

        if (first >= blockCount_ || count > blockCount_ - first) {
            return Error::InvalidBlock;
        }

        return write && readOnly_ ? Error::ReadOnly : Error::Success;
    }

    /* 只有明确为内存盘或直写缓存时，才能省略硬件 FLUSH。 */
    FlushAction BlockInfo::getFlushAction() const {
        if (!online_) {
            return FlushAction::Offline;
        }

        if (cacheMode_ == CacheMode::VolatileMemory ||
            cacheMode_ == CacheMode::WriteThrough) {
            return FlushAction::Complete;
        }

        if (supportsFlushCommand_) {
            return FlushAction::Command;
        }

        return cacheMode_ == CacheMode::WriteBack ? FlushAction::Unsupported
                                                  : FlushAction::Unknown;
    }

    /* 保存调用方提供的范围；缓冲区保持有效直到整批命令完成。 */
    BlockTransfer::BlockTransfer(uint64_t first, uint32_t count, void* buffer,
                                 size_t bytes, bool write)
        : first_(first),
          count_(count),
          buffer_(buffer),
          bytes_(bytes),
          write_(write) {
    }

    /* 读取驱动提交命令需要的范围、方向与数据。 */
    uint64_t BlockTransfer::getFirst() const {
        return first_;
    }

    uint32_t BlockTransfer::getCount() const {
        return count_;
    }

    void* BlockTransfer::getBuffer() const {
        return buffer_;
    }

    size_t BlockTransfer::getBytes() const {
        return bytes_;
    }

    bool BlockTransfer::isWrite() const {
        return write_;
    }

    TransferResult BlockTransfer::getResult() const {
        return result_;
    }

    /* 驱动记录已经确认完成的连续块数。 */
    void BlockTransfer::setResult(TransferResult result) {
        result_ = result;
    }

    /* 串行设备按收到的顺序执行；支持并行的驱动覆盖这个入口。 */
    void BlockBackend::submitBatch(BlockTransfer* requests, uint32_t count) {
        for (uint32_t index = 0; index < count; ++index) {
            BlockTransfer& request = requests[index];
            request.setResult(
                request.isWrite()
                    ? write(request.getFirst(), request.getCount(),
                            request.getBuffer(), request.getBytes())
                    : read(request.getFirst(), request.getCount(),
                           request.getBuffer(), request.getBytes()));
        }
    }

}  // namespace storage
