#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel {

class Process;

bool createPipeHandles(Process& process, uint64_t& readHandle,
                       uint64_t& writeHandle);
uint64_t duplicatePipeHandle(Process& source, Process& destination,
                             uint64_t handle,
                             uint64_t destinationHandle = 0);
int64_t readPipeHandle(Process& process, uint64_t handle, void* buffer,
                       size_t size);
int64_t writePipeHandle(Process& process, uint64_t handle, const void* buffer,
                        size_t size);
void closePipeHandleObject(void* object);

}  // namespace kernel
