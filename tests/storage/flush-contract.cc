#include <atomic>
#include <cstdio>
#include <latch>
#include <mutex>
#include <thread>

#include "service/device/storage/range_reply.h"

namespace {
using Error = os::Storage::Error;
using storage::CacheMode;
using storage::FlushAction;
using storage::TransferResult;

class TestMutex {
   public:
    std::mutex mutex;
    static inline std::atomic<unsigned> attempts{0};

    class Guard {
       public:
        explicit Guard(TestMutex& owner) : mutex(owner.mutex, std::defer_lock) {
            ++attempts;
            mutex.lock();
        }

       private:
        std::unique_lock<std::mutex> mutex;
    };
};

bool ordersWriteAndFlush() {
    storage::WriteOrder<TestMutex> order;
    std::latch writeEntered(1), releaseWrite(1), flushEntered(1),
        releaseFlush(1);
    std::atomic<unsigned> phase{0};
    bool earlierWriteFinished = false, flushFinished = false;
    std::thread write([&] {
        order.run([&] {
            phase = 1;
            writeEntered.count_down();
            releaseWrite.wait();
            phase = 2;
        });
    });
    writeEntered.wait();
    std::thread flush([&] {
        order.run([&] {
            earlierWriteFinished = phase == 2;
            phase                = 3;
            flushEntered.count_down();
            releaseFlush.wait();
            phase = 4;
        });
    });
    while (TestMutex::attempts < 2) std::this_thread::yield();
    const bool flushWaited = phase == 1;
    releaseWrite.count_down();
    flushEntered.wait();
    std::thread laterWrite(
        [&] { order.run([&] { flushFinished = phase == 4; }); });
    while (TestMutex::attempts < 3) std::this_thread::yield();
    releaseFlush.count_down();
    write.join();
    flush.join();
    laterWrite.join();
    return flushWaited && earlierWriteFinished && flushFinished;
}
}  // namespace

int main() {
    unsigned checks = 0, failed = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) ++failed;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    };
    auto deviceInfo = [](CacheMode mode, bool supportsFlush,
                         bool online = true) {
        return storage::BlockInfo{16,   512,          7, online, false,
                                  mode, supportsFlush};
    };
    check(storage::virtioCacheMode(false, 0, false) == CacheMode::Unknown &&
              storage::flushAction(deviceInfo(CacheMode::Unknown, false)) ==
                  FlushAction::Unknown,
          "missing flush guarantee");
    const auto writeThrough = storage::virtioCacheMode(true, 0, true);
    check(storage::flushAction(deviceInfo(writeThrough, false)) ==
              FlushAction::Complete,
          "confirmed write-through");
    const auto writeBack = storage::virtioCacheMode(false, 0, true);
    check(storage::flushAction(deviceInfo(writeBack, false)) ==
              FlushAction::Unsupported,
          "write-back without command");
    check(storage::flushAction(deviceInfo(writeBack, true)) ==
              FlushAction::Command,
          "write-back uses flush command");
    check(storage::flushAction(deviceInfo(CacheMode::VolatileMemory, false)) ==
              FlushAction::Complete,
          "memory barrier");
    check(storage::flushAction(deviceInfo(CacheMode::VolatileMemory, false,
                                          false)) == FlushAction::Offline,
          "offline flush");
    check(ordersWriteAndFlush(), "admitted write, flush, later write");

    os::IpcMessage reply{};
    storage::encodeRangeReply(reply, {Error::Timeout, 2}, 4, 512, true);
    auto result = storage::decodeRangeReply(reply, 4, 512, true);
    check(result.getError() == Error::Timeout && result.getCompleted() == 2 &&
              reply.length == 130,
          "confirmed partial read over IPC");
    --reply.length;
    result = storage::decodeRangeReply(reply, 4, 512, true);
    check(result.getError() == Error::Io && result.getCompleted() == 0,
          "truncated read prefix rejected");
    storage::encodeRangeReply(reply, {Error::Io, 1}, 4, 512, false);
    result = storage::decodeRangeReply(reply, 4, 512, false);
    check(result.getError() == Error::Io && result.getCompleted() == 1 &&
              reply.length == 2,
          "confirmed partial write over IPC");
    storage::encodeRangeReply(reply, {Error::Timeout, 0}, 4, 512, false);
    result = storage::decodeRangeReply(reply, 4, 512, false);
    check(result.getError() == Error::Timeout && result.getCompleted() == 0 &&
              reply.length == 1,
          "unknown completion retains legacy error reply");
    storage::encodeRangeReply(reply, {Error::Success, 3}, 4, 512, false);
    result = storage::decodeRangeReply(reply, 4, 512, false);
    check(result.getError() == Error::Io && result.getCompleted() == 0,
          "short success rejected");

    unsigned calls = 0;
    result = storage::transferBatches(9, 3, [&](uint32_t done, uint32_t count) {
        ++calls;
        return done == 0 ? TransferResult{Error::Success, count}
                         : TransferResult{Error::Timeout, 0};
    });
    check(result.getError() == Error::Timeout && result.getCompleted() == 3 &&
              calls == 2,
          "no retry or next batch after uncertain write");
    calls  = 0;
    result = storage::transferBatches(9, 3, [&](uint32_t done, uint32_t count) {
        ++calls;
        return done == 0 ? TransferResult{Error::Success, count}
                         : TransferResult{Error::Io, 1};
    });
    check(result.getError() == Error::Io && result.getCompleted() == 4 &&
              calls == 2,
          "earlier batches and confirmed failure prefix accumulate");
    std::printf("%u/%u passed\n", checks - failed, checks);
    return failed ? 1 : 0;
}
