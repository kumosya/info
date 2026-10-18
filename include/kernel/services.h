#pragma once

#include "os/abi.h"
#include "os/pci.h"
#include "os/core_service.h"

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
    static bool protectMemory(uint64_t address, uint64_t size, uint64_t protection);
    static int waitAddress(volatile uint32_t* address, uint32_t expected);
    static int wakeAddress(volatile uint32_t* address, uint32_t count);
    static int64_t duplicateHandle(uint64_t handle, uint32_t rights, uint32_t flags);
    static int restrictHandle(uint64_t handle, uint32_t rights);
    static int setHandleFlags(uint64_t handle, uint32_t flags);
    static int handleState(uint64_t handle);
    static uint64_t bootstrapHandle();
    static bool closeHandle(uint64_t handle);
    [[noreturn]] static void exitTask(int code);
    [[noreturn]] static void exitThread(int code);
    static void yield();
    static void sleep(uint64_t milliseconds);
    static uint64_t mapMemory(uint64_t address, uint64_t size, uint64_t protection);
    static uint64_t createThread(void (*entry)(), uint64_t stack);
    static uint64_t createThreadWithArgument(void (*entry)(void*), uint64_t stack, void* argument);
    static int send(uint64_t endpoint, os::IpcMessage& message, uint64_t deadline);
    static int receive(uint64_t endpoint, os::IpcMessage& message, uint64_t deadline);
    static int call(uint64_t endpoint, os::IpcMessage& request, os::IpcMessage& reply, uint64_t deadline);
    static int reply(uint64_t endpoint, uint64_t token, os::IpcMessage& reply);
    static int cancel(uint64_t endpoint, uint64_t thread);
    static uint64_t monotonicTime();
    static void log(const char* text);
    static int pciRequest(os::PciRequest& request);
    static int queryTask(uint64_t id, os::TaskInfo& info);
    static uint64_t mapMmio(const os::PciFunctionInfo& function, uint8_t bar, uint64_t address, uint64_t size);
    static int allocateDma(uint64_t address, os::DmaBufferInfo& info);
    static uint64_t bindIrq(uint8_t irq, uint64_t endpoint, bool levelTriggered);
    static int acknowledgeIrq(uint64_t handle);
    static int coreCall(os::CoreServiceKind kind, os::IpcMessage& request,
                        os::IpcMessage& reply, uint64_t deadline);
};
}  // namespace kernel
