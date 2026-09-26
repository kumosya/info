#pragma once

#include <stdint.h>

namespace kernel {

struct Thread;

void initTime();
uint64_t getMonotonicTime();
uint64_t getWallClockTime();
uint64_t getClockResolution();
uint64_t millisecondsToNanoseconds(uint64_t milliseconds);
uint64_t microsecondsToNanoseconds(uint64_t microseconds);
uint64_t getCpuRuntime(const Thread* thread);
bool deadlineReached(uint64_t now, uint64_t deadline);

}  // namespace kernel
