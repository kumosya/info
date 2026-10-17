#pragma once

#include <stddef.h>
#include <stdint.h>
#include <os/syscall.h>

namespace kernel {

enum class SyscallNumber : uint64_t {
    // Message queue
    OpenMessageQueue = OS_SYS_OPEN_MESSAGE_QUEUE,
    UnlinkMessageQueue = OS_SYS_UNLINK_MESSAGE_QUEUE,
    SendMessageQueue = OS_SYS_SEND_MESSAGE_QUEUE,
    ReceiveMessageQueue = OS_SYS_RECEIVE_MESSAGE_QUEUE,
    GetMessageQueueAttributes = OS_SYS_GET_MESSAGE_QUEUE_ATTRIBUTES,
    SetMessageQueueFlags = OS_SYS_SET_MESSAGE_QUEUE_FLAGS,
    RegisterMessageQueueNotification = OS_SYS_REGISTER_MESSAGE_QUEUE_NOTIFICATION,
    // Process
    GetStandardHandle = OS_SYS_GET_STANDARD_HANDLE,
    ExitTask = OS_SYS_EXIT_TASK,
    CreateThread = OS_SYS_CREATE_THREAD,
    ExitThread = OS_SYS_EXIT_THREAD,
    Yield = OS_SYS_YIELD,
    Sleep = OS_SYS_SLEEP,
    SpawnProgram = OS_SYS_SPAWN_PROGRAM,
    SpawnImage = OS_SYS_SPAWN_IMAGE,
    GetBootstrapHandle = OS_SYS_GET_BOOTSTRAP_HANDLE,
    GetTaskState = OS_SYS_GET_TASK_STATE,
    CloseHandle = OS_SYS_CLOSE_HANDLE,
    GetTaskId = OS_SYS_GET_TASK_ID,
    GetThreadId = OS_SYS_GET_THREAD_ID,
    GetTaskInfo = OS_SYS_GET_TASK_INFO,
    TerminateTask = OS_SYS_TERMINATE_TASK,
    SendSignal = OS_SYS_SEND_SIGNAL,
    SendThreadSignal = OS_SYS_SEND_THREAD_SIGNAL,
    CreateSession = OS_SYS_CREATE_SESSION,
    GetTaskIdentifiers = OS_SYS_GET_TASK_IDENTIFIERS,
    SetTaskGroup = OS_SYS_SET_TASK_GROUP,
    SetCredentials = OS_SYS_SET_CREDENTIALS,
    QueryTask = OS_SYS_QUERY_TASK,
    EnumerateTask = OS_SYS_ENUMERATE_TASK,
    ForkTask = OS_SYS_FORK_TASK,
    WaitChild = OS_SYS_WAIT_CHILD,
    ReplaceTaskImage = OS_SYS_REPLACE_TASK_IMAGE,
    WaitThread = OS_SYS_WAIT_THREAD,
    WaitAddress = OS_SYS_WAIT_ADDRESS,
    WakeAddress = OS_SYS_WAKE_ADDRESS,
    SetSignalAction = OS_SYS_SET_SIGNAL_ACTION,
    SetSignalMask = OS_SYS_SET_SIGNAL_MASK,
    GetPendingSignals = OS_SYS_GET_PENDING_SIGNALS,
    WaitSignal = OS_SYS_WAIT_SIGNAL,
    SignalReturn = OS_SYS_SIGNAL_RETURN,
    TerminalId = OS_SYS_TERMINAL_ID,
    CreateEndpoint = OS_SYS_CREATE_ENDPOINT,
    SendMessage = OS_SYS_SEND_MESSAGE,
    ReceiveMessage = OS_SYS_RECEIVE_MESSAGE,
    CallService = OS_SYS_CALL_SERVICE,
    ReplyMessage = OS_SYS_REPLY_MESSAGE,
    // Pipe and channel
    CreatePipe = OS_SYS_CREATE_PIPE,
    ReadPipe = OS_SYS_READ_PIPE,
    WritePipe = OS_SYS_WRITE_PIPE,
    CreateChannelPair = OS_SYS_CREATE_CHANNEL_PAIR,
    GetChannelIdentity = OS_SYS_GET_CHANNEL_IDENTITY,
    SetChannelNonblocking = OS_SYS_SET_CHANNEL_NONBLOCKING,
    SignalChannelEof = OS_SYS_SIGNAL_CHANNEL_EOF,
    // Device
    CallPciService = OS_SYS_CALL_PCI_SERVICE,
    MapDeviceMmio = OS_SYS_MAP_DEVICE_MMIO,
    AllocateDma = OS_SYS_ALLOCATE_DMA,
    BindIrq = OS_SYS_BIND_IRQ,
    AcknowledgeIrq = OS_SYS_ACKNOWLEDGE_IRQ,
    MapFramebuffer = OS_SYS_MAP_FRAMEBUFFER,
    GetMonotonicTime = OS_SYS_GET_MONOTONIC_TIME,
    GetWallClockTime = OS_SYS_GET_WALL_CLOCK_TIME,
    GetClockResolution = OS_SYS_GET_CLOCK_RESOLUTION,
    GetSystemInfo = OS_SYS_GET_SYSTEM_INFO,
    Log = OS_SYS_LOG,
    // Memory
    MapMemory = OS_SYS_MAP_MEMORY,
    UnmapMemory = OS_SYS_UNMAP_MEMORY,
    ProtectMemory = OS_SYS_PROTECT_MEMORY,
    OpenSharedMemory = OS_SYS_OPEN_SHARED_MEMORY,
    UnlinkSharedMemory = OS_SYS_UNLINK_SHARED_MEMORY,
    ResizeSharedMemory = OS_SYS_RESIZE_SHARED_MEMORY,
    MapSharedMemory = OS_SYS_MAP_SHARED_MEMORY,
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
