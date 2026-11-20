#include <array>
#include <cstdio>
#include <cstring>

#include "service/device/storage/block_backend.h"
#include "service/common/requests.h"

namespace {
using Error = os::Storage::Error;
using storage::CacheMode;
using storage::StopResult;
using storage::TransferResult;

constexpr TransferResult emptyTransfer{};
static_assert(emptyTransfer.getError() == Error::Success);
static_assert(emptyTransfer.getCompleted() == 0);

constexpr uint32_t blockSize  = 512;
constexpr uint32_t blockCount = 4;

class MemoryBackend final : public storage::BlockBackend {
   public:
    explicit MemoryBackend(CacheMode mode, bool supportsFlush)
        : mode_(mode), supportsFlush_(supportsFlush) {}

    storage::BlockInfo info() const override {
        return {blockCount, blockSize,     2, online_, false,
                mode_,      supportsFlush_};
    }

    TransferResult read(uint64_t first, uint32_t count, void* buffer,
                        size_t bytes) override {
        const auto error = storage::validateTransfer(info(), first, count,
                                                     buffer, bytes, false);
        if (error != Error::Success) return {error, 0};
        std::memcpy(buffer, media_.data() + first * blockSize,
                    count * blockSize);
        return {Error::Success, count};
    }

    TransferResult write(uint64_t first, uint32_t count, const void* buffer,
                         size_t bytes) override {
        const auto error = storage::validateTransfer(info(), first, count,
                                                     buffer, bytes, true);
        if (error != Error::Success) return {error, 0};
        if (first == timeoutBlock_) return {Error::Timeout, 0};
        std::memcpy(media_.data() + first * blockSize, buffer,
                    count * blockSize);
        return {Error::Success, count};
    }

    Error flush() override {
        if (!online_) return Error::DeviceOffline;
        return supportsFlush_ || mode_ == CacheMode::VolatileMemory
                   ? Error::Success
                   : Error::Io;
    }

    StopResult stop() override {
        online_ = false;
        return StopResult::Stopped;
    }

    void setTimeoutBlock(uint64_t block) { timeoutBlock_ = block; }

   private:
    CacheMode mode_;
    bool supportsFlush_;
    bool online_           = true;
    uint64_t timeoutBlock_ = UINT64_MAX;
    std::array<unsigned char, blockCount * blockSize> media_{};
};
}  // namespace

int main() {
    unsigned tests = 0, failures = 0;
    auto check = [&](bool ok, const char* name) {
        ++tests;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };

    MemoryBackend memory(CacheMode::VolatileMemory, false);
    MemoryBackend device(CacheMode::Unknown, true);
    MemoryBackend unknown(CacheMode::Unknown, false);
    storage::BlockBackend& backend = memory;
    const auto info                = backend.info();
    std::array<unsigned char, 2 * blockSize> input{}, output{};
    input.fill(0xa7);

    check(storage::capacityBytes(info) == blockCount * blockSize,
          "capacity in bytes");
    uint64_t logicalBlocks = 0;
    check(storage::virtioBlockCount(80, 4096, logicalBlocks) &&
              logicalBlocks == 10,
          "VirtIO sector units remain 512 on 4Kn");
    check(!storage::virtioBlockCount(81, 4096, logicalBlocks) &&
              !storage::virtioBlockCount(UINT64_MAX, 512, logicalBlocks),
          "reject non-integral and overflowing VirtIO capacities");
    check(os::StorageProtocol::rangeLimit(512, 64) == 8 &&
              os::StorageProtocol::rangeLimit(512, 7) == 7 &&
              os::StorageProtocol::rangeLimit(4096, 0) == 0,
          "range limit honors IPC and backend bounds");
    const storage::BlockInfo zeroBlockSize{
        blockCount, 0, 2, true, false, CacheMode::VolatileMemory, false};
    check(storage::capacityBytes(zeroBlockSize) == 0,
          "reject zero logical size");
    const storage::BlockInfo overflowingCapacity{
        UINT64_MAX / blockSize + 1, blockSize, 2, true, false,
        CacheMode::VolatileMemory,  false};
    check(storage::capacityBytes(overflowingCapacity) == 0,
          "reject capacity overflow");
    const storage::BlockInfo largeBlocks{
        blockCount, 4096, 2, true, false, CacheMode::VolatileMemory, false};
    check(
        storage::validateTransfer(largeBlocks, 0, 1, input.data(), input.size(),
                                  false) == Error::InvalidArgument,
        "buffer check uses reported logical block size");
    check(backend.read(0, 0, output.data(), output.size()).getError() ==
              Error::InvalidArgument,
          "reject zero length");
    check(backend.read(0, 1, nullptr, output.size()).getError() ==
              Error::InvalidArgument,
          "reject null buffer");
    check(backend.read(0, 2, output.data(), output.size() - 1).getError() ==
              Error::InvalidArgument,
          "reject short buffer");
    const storage::BlockInfo singleBlockTransfers{
        blockCount, blockSize, 1, true, false, CacheMode::VolatileMemory,
        false};
    check(storage::validateTransfer(singleBlockTransfers, 0, 2, output.data(),
                                    output.size(),
                                    false) == Error::InvalidArgument,
          "reject requests above backend limit");
    check(
        backend.read(UINT64_MAX, 1, output.data(), output.size()).getError() ==
            Error::InvalidBlock,
        "reject overflowing start block");
    check(backend.read(blockCount - 1, 2, output.data(), output.size())
                  .getError() == Error::InvalidBlock,
          "reject crossing end of device");
    const storage::BlockInfo readOnly{
        blockCount, blockSize, 2, true, true, CacheMode::VolatileMemory, false};
    check(storage::validateTransfer(readOnly, 0, 1, input.data(), input.size(),
                                    true) == Error::ReadOnly &&
              storage::validateTransfer(readOnly, 0, 1, input.data(),
                                        input.size(), false) == Error::Success,
          "read-only blocks writes only");

    uint64_t physical = UINT64_MAX;
    check(storage::translatePartitionRange(8, 2, 3, 2, 1, physical) &&
              physical == 4,
          "partition last block translates to parent");
    check(!storage::translatePartitionRange(8, 2, 3, 2, 2, physical) &&
              physical == 4,
          "reject crossing partition end");
    check(!storage::translatePartitionRange(UINT64_MAX, UINT64_MAX - 1, 2, 0, 1,
                                            physical),
          "reject overflowing parent extent");

    const auto written =
        backend.write(blockCount - 1, 1, input.data(), blockSize);
    const auto read = backend.read(blockCount - 1, 1, output.data(), blockSize);
    check(written.getError() == Error::Success && written.getCompleted() == 1 &&
              read.getError() == Error::Success && read.getCompleted() == 1 &&
              std::memcmp(input.data(), output.data(), blockSize) == 0,
          "last block round trip reports one logical block");
    check(!backend.info().supportsFlushCommand() &&
              backend.flush() == Error::Success,
          "volatile memory can order writes without a flush command");
    check(device.info().supportsFlushCommand() &&
              device.flush() == Error::Success && unknown.flush() == Error::Io,
          "unknown cache without flush fails");
    device.setTimeoutBlock(1);
    const auto first  = device.write(0, 1, input.data(), blockSize);
    const auto second = device.write(1, 1, input.data() + blockSize, blockSize);
    check(first.getError() == Error::Success &&
              second.getError() == Error::Timeout &&
              first.getCompleted() + second.getCompleted() == 1,
          "completed prefix excludes timed-out batch");
    check(backend.stop() == StopResult::Stopped &&
              backend.stop() == StopResult::Stopped &&
              !backend.info().isOnline() &&
              backend.read(0, 1, output.data(), output.size()).getError() ==
                  Error::DeviceOffline &&
              backend.flush() == Error::DeviceOffline,
          "stop is idempotent and denies new I/O");
    std::printf("%u/%u passed\n", tests - failures, tests);
    return failures ? 1 : 0;
}
