#pragma once

#include <cstddef>
#include <cstdint>
#include <os/syscall.h>

namespace kernel {

enum class SyscallNumber : uint64_t {
    // Message queue
    // Process
    GetStandardHandle = OS_SYS_GET_STANDARD_HANDLE,
    ExitTask = OS_SYS_EXIT_TASK,
    CreateThread = OS_SYS_CREATE_THREAD,
    ExitThread = OS_SYS_EXIT_THREAD,
    Yield = OS_SYS_YIELD,
    Sleep = OS_SYS_SLEEP,
    GetBootstrapHandle = OS_SYS_GET_BOOTSTRAP_HANDLE,
    CloseHandle = OS_SYS_CLOSE_HANDLE,
    ForkTask = OS_SYS_FORK_TASK,
    ReplaceTaskImage = OS_SYS_REPLACE_TASK_IMAGE,
    WaitAddress = OS_SYS_WAIT_ADDRESS,
    WakeAddress = OS_SYS_WAKE_ADDRESS,
    SendMessage = OS_SYS_SEND_MESSAGE,
    ReceiveMessage = OS_SYS_RECEIVE_MESSAGE,
    CallService = OS_SYS_CALL_SERVICE,
    ReplyMessage = OS_SYS_REPLY_MESSAGE,
    // Pipe and channel
    // Device
    // Memory
    MapMemory = OS_SYS_MAP_MEMORY,
    UnmapMemory = OS_SYS_UNMAP_MEMORY,
    ProtectMemory = OS_SYS_PROTECT_MEMORY,
    Count = OS_SYS_COUNT
};

enum class SyscallError : int64_t {
    InvalidArgument  = -1,
    InvalidHandle    = -2,
    PermissionDenied = -3,
    NotFound         = -4,
    OutOfMemory      = -5,
    WouldBlock       = -6,
    NotSupported     = -7,
    Busy             = -8,
    IoError          = -11
};

struct SyscallContext {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t userRip;
    uint64_t userRflags;
    uint64_t userRsp;
};

namespace syscall {
void init();
bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size);
bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size);

uint64_t dispatchSyscall(uint64_t syscallNumber, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6);
extern "C" void syscallEntry();
extern "C" void syscallDispatchEntry(SyscallContext* context);
}  // namespace syscall

}  // namespace kernel
