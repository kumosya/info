#pragma once

#include <stdint.h>

namespace kernel {

class Thread;

namespace time {

void init();
uint64_t getMonotonicTime();
uint64_t getWallClockTime();
uint64_t getClockResolution();
uint64_t milliseconds2Nanoseconds(uint64_t milliseconds);
uint64_t microseconds2Nanoseconds(uint64_t microseconds);
uint64_t getCpuRuntime(const Thread* thread);
bool deadlineReached(uint64_t now, uint64_t deadline);

}  // namespace time

}  // namespace kernel
