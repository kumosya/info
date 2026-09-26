#include "os/pty.h"

#include "os/abi.h"

namespace os {

bool createPty(PtyHandles& handles) {
    handles = {};
    return invokeSyscall(SyscallNumber::CreatePty,
                         reinterpret_cast<uint64_t>(&handles)) == 0;
}

bool getPtyAttributes(uint64_t handle, TerminalAttributes& attributes) {
    return invokeSyscall(SyscallNumber::GetPtyAttributes, handle,
                         reinterpret_cast<uint64_t>(&attributes)) == 0;
}

bool setPtyAttributes(uint64_t handle,
                      const TerminalAttributes& attributes) {
    return invokeSyscall(SyscallNumber::SetPtyAttributes, handle,
                         reinterpret_cast<uint64_t>(&attributes)) == 0;
}

bool getPtyWindowSize(uint64_t handle, TerminalWindowSize& size) {
    return invokeSyscall(SyscallNumber::GetPtyWindowSize, handle,
                         reinterpret_cast<uint64_t>(&size)) == 0;
}

bool setPtyWindowSize(uint64_t handle, const TerminalWindowSize& size) {
    return invokeSyscall(SyscallNumber::SetPtyWindowSize, handle,
                         reinterpret_cast<uint64_t>(&size)) == 0;
}

bool setPtyForegroundGroup(uint64_t handle, uint64_t processGroupId) {
    return invokeSyscall(SyscallNumber::SetPtyForegroundGroup, handle,
                         processGroupId) == 0;
}

bool setPtyNonblocking(uint64_t handle, bool enabled) {
    return invokeSyscall(SyscallNumber::SetPtyNonblocking, handle,
                         enabled ? 1 : 0) == 0;
}

}  // namespace os
