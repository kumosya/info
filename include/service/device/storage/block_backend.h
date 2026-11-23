#pragma once
#include <os/storage.h>

namespace storage {

    constexpr uint32_t virtioSectorBytes = 512;

    enum class CacheMode : uint8_t {
        Unknown,
        VolatileMemory,
        WriteThrough,
        WriteBack
    };

    enum class FlushAction : uint8_t;

    /* 保存真实设备能力，并根据这些能力校验传输和决定缓存刷新方式。 */
    class BlockInfo {
        public:
            constexpr BlockInfo() = default;

            constexpr BlockInfo(uint64_t blockCount, uint32_t logicalBlockSize,
                                uint32_t maxTransferBlocks, bool online,
                                bool readOnly, CacheMode cacheMode,
                                bool supportsFlushCommand)
                : blockCount_(blockCount),
                  logicalBlockSize_(logicalBlockSize),
                  maxTransferBlocks_(maxTransferBlocks),
                  online_(online),
                  readOnly_(readOnly),
                  cacheMode_(cacheMode),
                  supportsFlushCommand_(supportsFlushCommand) {
            }

            constexpr uint64_t getBlockCount() const {
                return blockCount_;
            }

            constexpr uint32_t getLogicalBlockSize() const {
                return logicalBlockSize_;
            }

            constexpr uint32_t getMaxTransferBlocks() const {
                return maxTransferBlocks_;
            }

            constexpr bool isOnline() const {
                return online_;
            }

            constexpr bool getReadOnly() const {
                return readOnly_;
            }

            constexpr CacheMode getCacheMode() const {
                return cacheMode_;
            }

            constexpr bool supportsFlushCommand() const {
                return supportsFlushCommand_;
            }

            uint64_t capacityBytes() const;
            os::Storage::Error validateTransfer(uint64_t first, uint32_t count,
                                                const void* buffer,
                                                size_t bytes, bool write) const;
            FlushAction getFlushAction() const;

        private:
            uint64_t blockCount_ = 0;
            uint32_t logicalBlockSize_ = 0;
            uint32_t maxTransferBlocks_ = 0;
            bool online_ = false;
            bool readOnly_ = false;
            CacheMode cacheMode_ = CacheMode::Unknown;
            bool supportsFlushCommand_ = false;
    };

    enum class FlushAction : uint8_t {
        Complete,
        Command,
        Unsupported,
        Unknown,
        Offline
    };

    template <typename Mutex>
    class WriteOrder {
            // 取得回调返回类型；这里是编译期表达式，不会访问空指针。
            template <typename Operation>
            using Result = decltype((*static_cast<Operation*>(nullptr))());

        public:
            template <typename Operation>
            Result<Operation> run(Operation operation) {
                typename Mutex::Guard guard(mutex_);
                return operation();
            }

        private:
            Mutex mutex_;
    };

    class TransferResult {
        public:
            constexpr TransferResult() = default;

            constexpr TransferResult(os::Storage::Error error,
                                     uint32_t completed)
                : error_(error), completed_(completed) {
            }

            constexpr os::Storage::Error getError() const {
                return error_;
            }

            // Only the confirmed contiguous prefix, in logical blocks.
            constexpr uint32_t getCompleted() const {
                return completed_;
            }

        private:
            os::Storage::Error error_ = os::Storage::Error::Success;
            uint32_t completed_ = 0;
    };

    enum class StopResult : uint8_t { Stopped, Quarantined, Unconfirmed };

    /* 一条交给驱动的块命令，保存数据缓冲区及该命令的完成结果。 */
    class BlockTransfer {
        public:
            BlockTransfer() = default;
            BlockTransfer(uint64_t first, uint32_t count, void* buffer,
                          size_t bytes, bool write);
            uint64_t getFirst() const;
            uint32_t getCount() const;
            void* getBuffer() const;
            size_t getBytes() const;
            bool isWrite() const;
            TransferResult getResult() const;
            void setResult(TransferResult result);

        private:
            uint64_t first_ = 0;
            uint32_t count_ = 0;
            void* buffer_ = nullptr;
            size_t bytes_ = 0;
            bool write_ = false;
            TransferResult result_{os::Storage::Error::Io, 0};
    };

    class BlockBackend {
        public:
            virtual void submitBatch(BlockTransfer* requests, uint32_t count);
            virtual BlockInfo info() const = 0;
            virtual TransferResult read(uint64_t first, uint32_t count,
                                        void* buffer, size_t bytes) = 0;
            virtual TransferResult write(uint64_t first, uint32_t count,
                                         const void* buffer, size_t bytes) = 0;
            virtual os::Storage::Error flush() = 0;
            virtual StopResult stop() = 0;

        protected:
            ~BlockBackend() = default;
    };

    inline bool translatePartitionRange(uint64_t parentBlocks, uint64_t start,
                                        uint64_t length, uint64_t first,
                                        uint32_t count, uint64_t& physical) {
        if (start >= parentBlocks || length > parentBlocks - start || !count ||
            first >= length || count > length - first) {
            return false;
        }

        physical = start + first;
        return true;
    }
}  // namespace storage
