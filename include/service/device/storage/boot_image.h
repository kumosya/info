#pragma once
#include <os/detail/client.h>
#include <os/runtime.h>
#include <os/sync.h>

#include "service/common/protocol.h"
#include "service/device/storage/block_backend.h"
#include "service/device/storage/range_reply.h"

namespace storage {

    // Only the reserved rootfs module is exposed by the kernel device endpoint.
    class BootImageDriver final : public BlockBackend {
        public:
            bool init();

            BlockInfo info() const override;

            TransferResult read(uint64_t first, uint32_t count, void* buffer,
                                size_t bytes) override;

            TransferResult write(uint64_t first, uint32_t count,
                                 const void* buffer, size_t bytes) override;

            os::Storage::Error flush() override;

            StopResult stop() override;

        private:
            static constexpr uint32_t logicalBlockSize = 512;
            static constexpr uint32_t wordsPerBlock =
                logicalBlockSize / sizeof(uint64_t);
            static_assert(2 + services::maximumStorageTransferBlocks *
                                  wordsPerBlock <=
                          os::ipcDataWordCount);

            BlockInfo infoLocked() const;

            TransferResult transfer(uint64_t block, uint32_t count,
                                    void* output, const void* input,
                                    size_t capacity, bool write);

            mutable os::Mutex lock_;
            uint64_t blocks_ = 0;
            bool online_ = false;
    };
}  // namespace storage
