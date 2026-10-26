#include "service/common/records.h"
#pragma once

#include <cstddef>
#include <cstdint>

#include <os/syscall.h>

namespace kernel {

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

#include <os/ipc.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>
#include <os/task.h>
#include "service/common/kernel-requests.h"

namespace kernel {

// Typed imports supplied only to immutable, trusted supervisor images.
class Services {
   public:
    static uint64_t resolve(const char* symbol);
    static uint64_t createEndpoint();
    static uint64_t coreHandle(os::CoreServiceKind kind);
    static uint64_t standardHandle(uint32_t index);
    static uint64_t threadId();
    static bool unmapMemory(uint64_t address, uint64_t size);
    static bool protectMemory(uint64_t address, uint64_t size,
                              uint64_t protection);
    static int waitAddress(volatile uint32_t* address, uint32_t expected);
    static int wakeAddress(volatile uint32_t* address, uint32_t count);
    static int64_t duplicateHandle(uint64_t handle, uint32_t rights,
                                   uint32_t flags);
    static int restrictHandle(uint64_t handle, uint32_t rights);
    static int setHandleFlags(uint64_t handle, uint32_t flags);
    static int handleState(uint64_t handle);
    static uint64_t bootstrapHandle();
    static bool closeHandle(uint64_t handle);
    [[noreturn]] static void exitTask(int code);
    [[noreturn]] static void exitThread(int code);
    static void yield();
    static void sleep(uint64_t milliseconds);
    static uint64_t mapMemory(uint64_t address, uint64_t size,
                              uint64_t protection);
    static uint64_t createThread(void (*entry)(), uint64_t stack);
    static uint64_t createThreadWithArgument(void (*entry)(void*),
                                             uint64_t stack, void* argument);
    static int send(uint64_t endpoint, os::IpcMessage& message,
                    uint64_t deadline);
    static int receive(uint64_t endpoint, os::IpcMessage& message,
                       uint64_t deadline);
    static int call(uint64_t endpoint, os::IpcMessage& request,
                    os::IpcMessage& reply, uint64_t deadline);
    static int reply(uint64_t endpoint, uint64_t token, os::IpcMessage& reply);
    static int cancel(uint64_t endpoint, uint64_t thread);
    static uint64_t monotonicTime();
    static void log(const char* text);
    static int queryTask(uint64_t id, os::wire::TaskInfo& info);
    static uint64_t mapMmio(const os::wire::PciFunctionInfo& function,
                            uint8_t bar, uint64_t address, uint64_t size);
    static int allocateDma(uint64_t device, uint64_t address,
                           os::wire::DmaBufferInfo& info);
    static uint64_t bindIrq(uint8_t irq, uint64_t endpoint,
                            bool levelTriggered);
    static int acknowledgeIrq(uint64_t handle);
};
}  // namespace kernel
