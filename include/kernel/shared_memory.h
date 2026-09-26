#pragma once

#include <stdint.h>

namespace kernel {

class Process;

uint64_t openSharedMemory(Process& process, const char* name, int flags);
uint64_t duplicateSharedMemoryHandle(Process& source, Process& destination,
                                     uint64_t handle,
                                     uint64_t destinationHandle = 0);
bool unlinkSharedMemory(const char* name);
bool resizeSharedMemory(Process& process, uint64_t handle, uint64_t size);
uint64_t mapSharedMemory(Process& process, uint64_t handle, uint64_t address,
                         uint64_t size, uint64_t offset,
                         uint64_t protection);
bool releaseSharedMemoryMapping(Process& process, uint64_t address,
                                uint64_t size);
bool sharedMemoryProtectionAllowed(Process& process, uint64_t address,
                                   uint64_t size, uint64_t protection);
void releaseProcessSharedMemoryMappings(Process& process);
bool cloneProcessSharedMemoryMappings(Process& source, Process& destination);
void closeSharedMemoryHandleObject(void* object);

}  // namespace kernel
