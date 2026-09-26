#pragma once

#include <stdint.h>

#include "kernel/boot.h"

namespace kernel {

void initPhysicalMemory(const BootInfo* bootInfo);
uint64_t allocatePhysicalPage();
void freePhysicalPage(uint64_t physicalAddress);
uint64_t getTotalMemoryBytes();
uint64_t getFreeMemoryBytes();

}  // namespace kernel
