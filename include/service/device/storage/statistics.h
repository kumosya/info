#pragma once

#include <os/storage.h>

namespace storage {
enum class IoDirection : uint8_t { None, Read, Write, Flush };

template <typename Lock>
class RequestCounters {
   public:
    void begin() {
        typename Lock::Guard guard(lock_);
        add(stats_.inFlight, 1);
    }

    void finish(IoDirection direction, os::Storage::Error error,
                uint64_t confirmedBytes) {
        using Error = os::Storage::Error;
        typename Lock::Guard guard(lock_);
        --stats_.inFlight;
        add(stats_.completedRequests, 1);
        if (error != Error::Success) add(stats_.errors, 1);
        if (error == Error::Timeout) add(stats_.timeouts, 1);
        if (direction == IoDirection::Read)
            add(stats_.readBytes, confirmedBytes);
        if (direction == IoDirection::Write)
            add(stats_.writtenBytes, confirmedBytes);
    }

    os::Storage::Statistics snapshot() const {
        typename Lock::Guard guard(lock_);
        return stats_;
    }

   private:
    static void add(uint64_t& total, uint64_t value) {
        total = value > UINT64_MAX - total ? UINT64_MAX : total + value;
    }

    mutable Lock lock_;
    os::Storage::Statistics stats_{};
};
}  // namespace storage
