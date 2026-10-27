#pragma once

#include <cstddef>
#include <cstdint>

#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/syscall.h>
#include <os/task.h>

namespace kernel::task {

enum class SyscallNumber : uint64_t {
    SendMessage    = OS_SYS_SEND_MESSAGE,
    ReceiveMessage = OS_SYS_RECEIVE_MESSAGE,
    CallService    = OS_SYS_CALL_SERVICE,
    ReplyMessage   = OS_SYS_REPLY_MESSAGE,
    WaitAddress    = OS_SYS_WAIT_ADDRESS,
    WakeAddress    = OS_SYS_WAKE_ADDRESS,
    Count          = OS_SYS_COUNT
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
    IoError          = -11,
    BadAddress       = -15
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
bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size);
bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size);

uint64_t dispatchSyscall(uint64_t syscallNumber, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6);
extern "C" void syscallEntry();
extern "C" void syscallDispatchEntry(SyscallContext* context);

// Typed imports supplied only to immutable, trusted supervisor images.

uint64_t serviceResolve(const char* symbol);
uint64_t serviceCreateEndpoint();
uint64_t serviceCoreHandle(os::CoreServiceKind kind);
uint64_t serviceStandardHandle(uint32_t index);
uint64_t serviceThreadId();
bool serviceUnmapMemory(uint64_t address, uint64_t size);
bool serviceProtectMemory(uint64_t address, uint64_t size, uint64_t protection);
int serviceWaitAddress(volatile uint32_t* address, uint32_t expected);
int serviceWakeAddress(volatile uint32_t* address, uint32_t count);
int64_t serviceDuplicateHandle(uint64_t handle, uint32_t rights,
                               uint32_t flags);
int serviceRestrictHandle(uint64_t handle, uint32_t rights);
int serviceSetHandleFlags(uint64_t handle, uint32_t flags);
int serviceHandleState(uint64_t handle);
uint64_t serviceBootstrapHandle();
bool serviceCloseHandle(uint64_t handle);
[[noreturn]] void serviceExitTask(int code);
[[noreturn]] void serviceExitThread(int code);
void serviceYield();
void serviceSleep(uint64_t milliseconds);
uint64_t serviceMapMemory(uint64_t address, uint64_t size, uint64_t protection);
uint64_t serviceCreateThread(void (*entry)(), uint64_t stack);
uint64_t serviceCreateThreadWithArgument(void (*entry)(void*), uint64_t stack,
                                         void* argument);
int serviceSend(uint64_t endpoint, os::IpcMessage& message, uint64_t deadline);
int serviceReceive(uint64_t endpoint, os::IpcMessage& message,
                   uint64_t deadline);
int serviceCall(uint64_t endpoint, os::IpcMessage& request,
                os::IpcMessage& reply, uint64_t deadline);
int serviceReply(uint64_t endpoint, uint64_t token, os::IpcMessage& reply);
int serviceCancel(uint64_t endpoint, uint64_t thread);
uint64_t serviceMonotonicTime();
void serviceLog(const char* text);
int serviceQueryTask(uint64_t id, os::wire::TaskInfo& info);
uint64_t serviceMapMmio(const os::wire::PciFunctionInfo& function, uint8_t bar,
                        uint64_t address, uint64_t size);
int serviceAllocateDma(uint64_t device, uint64_t address,
                       os::wire::DmaBufferInfo& info);
uint64_t serviceBindIrq(uint8_t irq, uint64_t endpoint, bool levelTriggered);
int serviceAcknowledgeIrq(uint64_t handle);

}  // namespace kernel::task
