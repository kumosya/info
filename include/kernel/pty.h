#pragma once

#include <stddef.h>
#include <stdint.h>

#include "os/pty.h"

namespace kernel {

class Process;

bool createPtyHandles(Process& process, uint64_t& masterHandle,
                      uint64_t& slaveHandle);
uint64_t duplicatePtyHandle(Process& source, Process& destination,
                            uint64_t handle,
                            uint64_t destinationHandle = 0);
int64_t readPtyHandle(Process& process, uint64_t handle, void* buffer,
                      size_t size);
int64_t writePtyHandle(Process& process, uint64_t handle, const void* buffer,
                       size_t size);
bool getPtyAttributes(Process& process, uint64_t handle,
                      os::TerminalAttributes& attributes);
bool setPtyAttributes(Process& process, uint64_t handle,
                      const os::TerminalAttributes& attributes);
bool getPtyWindowSize(Process& process, uint64_t handle,
                      os::TerminalWindowSize& size);
bool setPtyWindowSize(Process& process, uint64_t handle,
                      const os::TerminalWindowSize& size);
bool setPtyForegroundGroup(Process& process, uint64_t handle,
                           uint64_t processGroupId);
bool setPtyNonblocking(Process& process, uint64_t handle, bool enabled);
void closePtyHandleObject(void* object);

}  // namespace kernel
