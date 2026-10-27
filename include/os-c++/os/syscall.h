#pragma once
#ifndef _OS_SYSCALL_H
#define _OS_SYSCALL_H

/* Shared Ring 3 syscall ABI. 0 is invalid. */
#define OS_SYS_BASE 1U

/* Endpoint IPC */
#define OS_SYS_SEND_MESSAGE 1U
#define OS_SYS_RECEIVE_MESSAGE 2U
#define OS_SYS_CALL_SERVICE 3U
#define OS_SYS_REPLY_MESSAGE 4U

/* Address synchronization keeps its existing entry numbers. */
#define OS_SYS_WAIT_ADDRESS 18U
#define OS_SYS_WAKE_ADDRESS 19U

#define OS_SYS_COUNT 6U

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

/* Native x86-64 signal ABI. Keep in sync with POSIX ucontext_t/siginfo_t. */
typedef struct {
    int signo, code, error, pid;
    unsigned uid;
    void* address;
    int status;
    int64_t value;
} OsSignalInfo;

typedef struct {
    void* pointer;
    int flags;
    size_t size;
} OsSignalStack;
typedef struct {
    int64_t registers[23];
    void* fpregs;
    uint64_t reserved[8];
} OsMachineContext;
typedef struct {
    uint64_t flags;
    void* link;
    OsSignalStack stack;
    OsMachineContext machine;
    uint64_t mask;
    uint64_t fpstate[64];
} OsUserContext;

enum {
    OS_REG_R8,
    OS_REG_R9,
    OS_REG_R10,
    OS_REG_R11,
    OS_REG_R12,
    OS_REG_R13,
    OS_REG_R14,
    OS_REG_R15,
    OS_REG_RDI,
    OS_REG_RSI,
    OS_REG_RBP,
    OS_REG_RBX,
    OS_REG_RDX,
    OS_REG_RAX,
    OS_REG_RCX,
    OS_REG_RSP,
    OS_REG_RIP,
    OS_REG_EFL,
    OS_REG_CSGSFS,
    OS_REG_ERR,
    OS_REG_TRAPNO,
    OS_REG_OLDMASK,
    OS_REG_CR2
};

#ifdef __cplusplus
#include <cstdint>
namespace os {
enum class SyscallNumber : uint64_t {
    SendMessage    = OS_SYS_SEND_MESSAGE,
    ReceiveMessage = OS_SYS_RECEIVE_MESSAGE,
    CallService    = OS_SYS_CALL_SERVICE,
    ReplyMessage   = OS_SYS_REPLY_MESSAGE,
    WaitAddress    = OS_SYS_WAIT_ADDRESS,
    WakeAddress    = OS_SYS_WAKE_ADDRESS,
    Count          = OS_SYS_COUNT
};
enum class ErrorCode : int64_t {
    Success          = 0,
    InvalidArgument  = -1,
    InvalidHandle    = -2,
    PermissionDenied = -3,
    NotFound         = -4,
    OutOfMemory      = -5,
    WouldBlock       = -6,
    NotSupported     = -7,
    Busy             = -8,
    Timeout          = -9,
    Interrupted      = -10,
    IoError          = -11,
    Cancelled        = -12,
    EndpointClosed   = -13,
    PeerDied         = -14,
    BadAddress       = -15
};
struct IpcMessage;
int waitAddress(volatile uint32_t* address, uint32_t expected);
int wakeAddress(volatile uint32_t* address, uint32_t count);
int sendMessage(uint64_t endpoint, IpcMessage& message,
                uint64_t deadline = UINT64_MAX);
int receiveMessage(uint64_t endpoint, IpcMessage& message,
                   uint64_t deadline = UINT64_MAX);
int callService(uint64_t endpoint, IpcMessage& request, IpcMessage& reply,
                uint64_t deadline = UINT64_MAX);
int replyMessage(uint64_t endpoint, uint64_t token, IpcMessage& reply);

int64_t invokeSyscall(SyscallNumber number, uint64_t arg1 = 0,
                      uint64_t arg2 = 0, uint64_t arg3 = 0, uint64_t arg4 = 0,
                      uint64_t arg5 = 0, uint64_t arg6 = 0);
}  // namespace os
#endif

#endif  // !__ASSEMBLER__
#endif
