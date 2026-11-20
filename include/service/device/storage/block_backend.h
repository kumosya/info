#pragma once

#include <os/storage.h>

namespace storage {
constexpr uint32_t virtioSectorBytes = 512;

inline bool virtioBlockCount(uint64_t sectors, uint32_t logicalBytes,
                             uint64_t& blocks) {
    if (logicalBytes < virtioSectorBytes ||
        (logicalBytes & (logicalBytes - 1)) || !sectors ||
        sectors > UINT64_MAX / virtioSectorBytes)
        return false;
    const uint32_t sectorsPerBlock = logicalBytes / virtioSectorBytes;
    if (sectors % sectorsPerBlock) return false;
    blocks = sectors / sectorsPerBlock;
    return true;
}

enum class CacheMode : uint8_t {
    Unknown,
    VolatileMemory,
    WriteThrough,
    WriteBack
};

constexpr CacheMode virtioCacheMode(bool hasCacheConfig, uint8_t writeback,
                                    bool hasFlush) {
    if (hasCacheConfig) {
        if (writeback == 0) return CacheMode::WriteThrough;
        if (writeback == 1) return CacheMode::WriteBack;
        return CacheMode::Unknown;
    }
    return hasFlush ? CacheMode::WriteBack : CacheMode::Unknown;
}

class BlockInfo {
   public:
    constexpr BlockInfo() = default;
    constexpr BlockInfo(uint64_t blockCount, uint32_t logicalBlockSize,
                        uint32_t maxTransferBlocks, bool online, bool readOnly,
                        CacheMode cacheMode, bool supportsFlushCommand)
        : blockCount_(blockCount),
          logicalBlockSize_(logicalBlockSize),
          maxTransferBlocks_(maxTransferBlocks),
          online_(online),
          readOnly_(readOnly),
          cacheMode_(cacheMode),
          supportsFlushCommand_(supportsFlushCommand) {}

    constexpr uint64_t getBlockCount() const { return blockCount_; }
    constexpr uint32_t getLogicalBlockSize() const { return logicalBlockSize_; }
    constexpr uint32_t getMaxTransferBlocks() const {
        return maxTransferBlocks_;
    }
    constexpr bool isOnline() const { return online_; }
    constexpr bool getReadOnly() const { return readOnly_; }
    constexpr CacheMode getCacheMode() const { return cacheMode_; }
    constexpr bool supportsFlushCommand() const {
        return supportsFlushCommand_;
    }

   private:
    uint64_t blockCount_        = 0;
    uint32_t logicalBlockSize_  = 0;
    uint32_t maxTransferBlocks_ = 0;
    bool online_                = false;
    bool readOnly_              = false;
    CacheMode cacheMode_        = CacheMode::Unknown;
    bool supportsFlushCommand_  = false;
};

enum class FlushAction : uint8_t {
    Complete,
    Command,
    Unsupported,
    Unknown,
    Offline
};

constexpr FlushAction flushAction(const BlockInfo& info) {
    if (!info.isOnline()) return FlushAction::Offline;
    if (info.getCacheMode() == CacheMode::VolatileMemory ||
        info.getCacheMode() == CacheMode::WriteThrough)
        return FlushAction::Complete;
    if (info.supportsFlushCommand()) return FlushAction::Command;
    return info.getCacheMode() == CacheMode::WriteBack
               ? FlushAction::Unsupported
               : FlushAction::Unknown;
}

template <typename Mutex>
class WriteOrder {
   public:
    template <typename Operation>
    auto run(Operation operation) {
        typename Mutex::Guard guard(mutex_);
        return operation();
    }

   private:
    Mutex mutex_;
};

class TransferResult {
   public:
    constexpr TransferResult() = default;
    constexpr TransferResult(os::Storage::Error error, uint32_t completed)
        : error_(error), completed_(completed) {}

    constexpr os::Storage::Error getError() const { return error_; }
    // Only the confirmed contiguous prefix, in logical blocks.
    constexpr uint32_t getCompleted() const { return completed_; }

   private:
    os::Storage::Error error_ = os::Storage::Error::Success;
    uint32_t completed_       = 0;
};

enum class StopResult : uint8_t { Stopped, Quarantined, Unconfirmed };

class BlockBackend {
   public:
    virtual BlockInfo info() const                                 = 0;
    virtual TransferResult read(uint64_t first, uint32_t count, void* buffer,
                                size_t bytes)                      = 0;
    virtual TransferResult write(uint64_t first, uint32_t count,
                                 const void* buffer, size_t bytes) = 0;
    virtual os::Storage::Error flush()                             = 0;
    virtual StopResult stop()                                      = 0;

   protected:
    ~BlockBackend() = default;
};

// A zero result denotes invalid geometry.
constexpr uint64_t capacityBytes(const BlockInfo& info) {
    if (!info.getLogicalBlockSize() ||
        info.getBlockCount() > UINT64_MAX / info.getLogicalBlockSize())
        return 0;
    return info.getBlockCount() * info.getLogicalBlockSize();
}

inline os::Storage::Error validateTransfer(const BlockInfo& info,
                                           uint64_t first, uint32_t count,
                                           const void* buffer, size_t bytes,
                                           bool write) {
    using Error = os::Storage::Error;
    if (!info.isOnline()) return Error::DeviceOffline;
    if (!capacityBytes(info) || !info.getMaxTransferBlocks() || !buffer ||
        !count || count > info.getMaxTransferBlocks() ||
        count > bytes / info.getLogicalBlockSize())
        return Error::InvalidArgument;
    if (first >= info.getBlockCount() || count > info.getBlockCount() - first)
        return Error::InvalidBlock;
    if (write && info.getReadOnly()) return Error::ReadOnly;
    return Error::Success;
}

inline bool translatePartitionRange(uint64_t parentBlocks, uint64_t start,
                                    uint64_t length, uint64_t first,
                                    uint32_t count, uint64_t& physical) {
    if (start >= parentBlocks || length > parentBlocks - start || !count ||
        first >= length || count > length - first)
        return false;
    physical = start + first;
    return true;
}
}  // namespace storage
