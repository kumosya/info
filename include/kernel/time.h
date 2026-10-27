#pragma once

#include <cstdint>

#include "kernel/atomic.h"
#include "kernel/sync.h"

namespace kernel::task {
class Thread;
}

namespace kernel::time {

class Clock {
   public:
    void init();
    uint64_t getMonotonicTime();
    uint64_t getWallClockTime();
    uint64_t getClockResolution() const;

   private:
    uint64_t readWallClockSeconds();
    uint64_t scaleTsc(uint64_t cycles) const;
    Atomic<uint64_t> last_monotonic_nanoseconds{0};
    kernel::task::SpinLock rtc_lock;
    uint64_t tsc_frequency = 0, boot_tsc = 0, wall_clock_base_nanoseconds = 0;
};
Clock& clock();

void initClock();
uint64_t getMonotonicTime();
uint64_t getWallClockTime();
uint64_t getClockResolution();
uint64_t milliseconds2Nanoseconds(uint64_t milliseconds);
uint64_t microseconds2Nanoseconds(uint64_t microseconds);
uint64_t getCpuRuntime(const kernel::task::Thread* thread);
bool deadlineReached(uint64_t now, uint64_t deadline);

}  // namespace kernel::time
