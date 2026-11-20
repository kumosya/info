#pragma once

#include "service/common/channel.h"
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/device/storage/block_backend.h"
#include "service/device/storage/range_reply.h"
#include <os/sync.h>

namespace storage {
// Only the reserved rootfs module is exposed by the kernel device endpoint.
class BootImageDriver final : public BlockBackend {
   public:
    bool init() {
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(os::DeviceOperation::BootImageInfo);
        const auto result =
            services::RequestChannel(os::CoreServiceKind::Device)
                .result(request, reply);
        blocks_ = result > 0 ? result : 0;
        online_ = blocks_ != 0;
        return online_;
    }

    BlockInfo info() const override {
        os::Mutex::Guard guard(lock_);
        return infoLocked();
    }

    TransferResult read(uint64_t first, uint32_t count, void* buffer,
                        size_t bytes) override {
        return transfer(first, count, buffer, nullptr, bytes, false);
    }

    TransferResult write(uint64_t first, uint32_t count, const void* buffer,
                         size_t bytes) override {
        return transfer(first, count, nullptr, buffer, bytes, true);
    }

    os::Storage::Error flush() override {
        os::Mutex::Guard guard(lock_);
        // The kernel copy has completed before a transfer releases the lock.
        return online_ ? os::Storage::Error::Success
                       : os::Storage::Error::DeviceOffline;
    }

    StopResult stop() override {
        os::Mutex::Guard guard(lock_);
        online_ = false;
        return StopResult::Stopped;
    }

   private:
    static constexpr uint32_t logicalBlockSize = 512;
    static constexpr uint32_t wordsPerBlock =
        logicalBlockSize / sizeof(uint64_t);
    static_assert(2 + os::maximumStorageTransferBlocks * wordsPerBlock <=
                  os::ipcDataWordCount);

    BlockInfo infoLocked() const {
        return {blocks_,
                logicalBlockSize,
                os::maximumStorageTransferBlocks,
                online_,
                false,
                CacheMode::VolatileMemory,
                false};
    }

    TransferResult transfer(uint64_t block, uint32_t count, void* output,
                            const void* input, size_t capacity, bool write) {
        using Error = os::Storage::Error;
        os::Mutex::Guard guard(lock_);
        const auto error =
            validateTransfer(infoLocked(), block, count, write ? input : output,
                             capacity, write);
        if (error != Error::Success) return {error, 0};

        const uint32_t bytes = count * logicalBlockSize;
        const uint32_t words = count * wordsPerBlock;
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(write ? os::DeviceOperation::BootImageWrite
                                        : os::DeviceOperation::BootImageRead);
        request.data[0] = block;
        request.data[1] = count;
        request.length  = write ? 2 + words : 2;
        if (write) os::Runtime::copyBytes(&request.data[2], input, bytes);
        const auto status =
            services::RequestChannel(os::CoreServiceKind::Device)
                .result(request, reply);
        if (status) return {transportError(status), 0};
        if (reply.length < (write ? 2 : 2 + words) || reply.data[1] != bytes)
            return {Error::Io, 0};
        if (!write) os::Runtime::copyBytes(output, &reply.data[2], bytes);
        return {Error::Success, count};
    }

    mutable os::Mutex lock_;
    uint64_t blocks_ = 0;
    bool online_     = false;
};
}  // namespace storage
