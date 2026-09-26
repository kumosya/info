#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel {

enum class SyscallNumber : uint64_t {
    ExitProcess        = 0,
    CreateThread       = 1,
    ExitThread         = 2,
    Yield              = 3,
    Sleep              = 4,
    MapMemory          = 5,
    UnmapMemory        = 6,
    CreateEndpoint     = 7,
    SendMessage        = 8,
    ReceiveMessage     = 9,
    CallService        = 10,
    ReplyMessage       = 11,
    SpawnProgram       = 12,
    GetBootstrapHandle = 13,
    Log                = 14,
    EnumeratePci       = 15,
    ReadPciConfig      = 16,
    WritePciConfig     = 17,
    MapDeviceMmio      = 18,
    AllocateDma        = 19,
    BindIrq            = 20,
    GetProcessState    = 21,
    CloseHandle        = 22,
    GetProcessId       = 23,
    GetThreadId        = 24,
    GetMonotonicTime   = 25,
    GetWallClockTime   = 26,
    ReadIoPort         = 27,
    WriteIoPort        = 28,
    MapFramebuffer     = 29,
    SpawnImage         = 30,
    CreatePipe         = 31,
    ReadPipe           = 32,
    WritePipe          = 33,
    GetStandardHandle  = 34,
    GetProcessInfo     = 35,
    TerminateProcess   = 36,
    CreatePty          = 37,
    GetPtyAttributes   = 38,
    SetPtyAttributes   = 39,
    GetPtyWindowSize   = 40,
    SetPtyWindowSize   = 41,
    SetPtyForegroundGroup = 42,
    SetPtyNonblocking  = 43,
    SendSignal         = 44,
    CreateSession      = 45,
    GetProcessIdentifiers = 46,
    GetClockResolution = 47,
    PosixUnsupported = 48,
    WaitThread = 49,
    WaitAddress = 50,
    WakeAddress = 51,
    SetSignalAction = 52,
    SetSignalMask = 53,
    GetPendingSignals = 54,
    WaitSignal = 55,
    SignalReturn = 56,
    OpenMessageQueue = 57,
    UnlinkMessageQueue = 58,
    SendMessageQueue = 59,
    ReceiveMessageQueue = 60,
    GetMessageQueueAttributes = 61,
    SetMessageQueueFlags = 62,
    ProtectMemory = 63,
    OpenSharedMemory = 64,
    UnlinkSharedMemory = 65,
    ResizeSharedMemory = 66,
    MapSharedMemory = 67,
    SendThreadSignal = 68,
    RegisterMessageQueueNotification = 69,
    ForkProcess = 70,
    WaitChild = 71,
    ReplaceProcessImage = 72,
    Count
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

void initSyscall();
uint64_t dispatchSyscall(uint64_t syscallNumber, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6);
bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size);
bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size);

extern "C" void syscallEntry();
extern "C" void syscallDispatchEntry(SyscallContext* context);

}  // namespace kernel
