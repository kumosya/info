#pragma once

#include <stddef.h>
#include <stdint.h>

#include "kernel/boot.h"

namespace kernel::memory {

void initPhy(const BootInfo* bootInfo);
uint64_t allocatePhysicalPage();
void freePhysicalPage(uint64_t physicalAddress);
uint64_t getTotalMemoryBytes();
uint64_t getFreeMemoryBytes();

}  // namespace kernel::memory

namespace kernel::heap {

void init();
void* kernelAllocate(size_t size);
void kernelFree(void* address);

}  // namespace kernel::heap

namespace kernel {

class Task;

class SharedMemory {
   public:
    static uint64_t open(Task& task, const char* name, int flags);
    static uint64_t duplicateHandle(Task& source, Task& destination,
                                    uint64_t handle,
                                    uint64_t destinationHandle = 0);
    static bool unlink(const char* name);
    static bool resize(Task& task, uint64_t handle, uint64_t size);
    static uint64_t map(Task& task, uint64_t handle, uint64_t address,
                        uint64_t size, uint64_t offset, uint64_t protection);
    static bool releaseMapping(Task& task, uint64_t address, uint64_t size);
    static bool protectionAllowed(Task& task, uint64_t address, uint64_t size,
                                  uint64_t protection);
    static void releaseTaskMappings(Task& task);
    static bool cloneTaskMappings(Task& source, Task& destination);
    static void closeHandleObject(void* object);
};

}  // namespace kernel
