/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/syscall/services.cc
 * Typed kernel imports and the trusted-image symbol allowlist.
 */

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/records.h"

namespace kernel::task {

namespace {
// Resolve imports against the real current task and retain through use;
// imported handle numbers never denote raw endpoint pointers.
HandleTable::Pin endpoint(uint64_t handle, uint32_t rights) {
    return HandleTable::Pin(task::getCurrent()->handles(), handle,
                            HandleType::Endpoint, rights);
}
// Legacy privileged device import gate: both Sys execution and Device
// capability are required, not just knowledge of an imported symbol.
bool deviceAccess() {
    auto* task = task::getCurrent();
    return task && task->isSystemService() &&
           task->hasCapability(taskCapabilityDevice);
}
}  // namespace

// Install a callable core endpoint in the current process table. Concrete
// services still apply operation-specific permission checks.
uint64_t serviceCoreHandle(os::CoreServiceKind kind) {
    return coreServices().getHandle(*task::getCurrent(), kind);
}
// Current process standard stream handle at the requested index.
uint64_t serviceStandardHandle(uint32_t index) {
    return task::getCurrent()->standardHandle(index);
}
// Current executing TID, independent of its process main thread.
uint64_t serviceThreadId() { return currentThread()->getId(); }
// Enforce word alignment and translate compare mismatch to WouldBlock for the
// trusted import ABI, preserving the same physical-key wait mechanism.
int serviceWaitAddress(volatile uint32_t* address, uint32_t expected) {
    if ((reinterpret_cast<uint64_t>(address) & 3) != 0) return -1;
    const int result = task::waitOnUserAddress(address, expected);
    return result < 0 ? -1 : result > 0 ? -6 : 0;
}
// Wake at most count waiters on an aligned synchronization word.
int serviceWakeAddress(volatile uint32_t* address, uint32_t count) {
    return (reinterpret_cast<uint64_t>(address) & 3)
               ? -1
               : task::wakeUserAddress(address, count);
}
// Duplicate within the current table with subset rights and validated flags.
int64_t serviceDuplicateHandle(uint64_t handle, uint32_t rights,
                               uint32_t flags) {
    const uint64_t result =
        task::getCurrent()->handles().duplicate(handle, rights, flags);
    return result ? static_cast<int64_t>(result) : -1;
}
// Narrow one handle slot; this cannot grant any additional capability.
int serviceRestrictHandle(uint64_t handle, uint32_t rights) {
    return task::getCurrent()->handles().restrictRights(handle, rights) ? 0
                                                                        : -3;
}
// Set validated inheritance/close-on-exec bits on the current table slot.
int serviceSetHandleFlags(uint64_t handle, uint32_t flags) {
    return task::getCurrent()->handles().setFlags(handle, flags) ? 0 : -1;
}
// Pin a real endpoint before reading closure state; a stale handle is an
// error, not evidence that some new object in its old slot is closed.
int serviceHandleState(uint64_t handle) {
    auto pin    = endpoint(handle, 0);
    auto* value = pin.as<Endpoint>();
    return value ? (value->isClosed() ? 1 : 0) : -2;
}
// Create a caller-owned endpoint and install send/receive/manage rights.
uint64_t serviceCreateEndpoint() {
    auto* value = Endpoint::create();
    return value ? task::getCurrent()->handles().add(value,
                                                     HandleType::Endpoint, 7)
                 : 0;
}
// Current process user-registry bootstrap handle; this is not kernelHandle.
uint64_t serviceBootstrapHandle() {
    return task::getCurrent()->bootstrapHandle();
}
// Drop a current-table slot; shared object lifetime may continue elsewhere.
bool serviceCloseHandle(uint64_t handle) {
    return task::getCurrent()->handles().close(handle);
}
// Nonreturning imported process exit; serviceExitThread exits only one thread.
// Neither operation frees the stack currently executing it.
void serviceExitTask(int code) { task::exitCurrent(code); }
// Nonreturning exit of the importing execution thread.
void serviceExitThread(int code) { exitCurrentThread(code); }
// Let the current runnable thread re-enter scheduler competition.
void serviceYield() { task::yieldThread(); }
// Sleep duration in milliseconds; zero yields instead of registering sleep.
void serviceSleep(uint64_t milliseconds) { sleepCurrentThread(milliseconds); }
// Adapt the parameterless imported entry to the argument-bearing creator.
uint64_t serviceCreateThread(void (*entry)(), uint64_t stack) {
    return serviceCreateThreadWithArgument(
        reinterpret_cast<void (*)(void*)>(entry), stack, nullptr);
}
// Create detached first, preserve the new ID, then enqueue. Another CPU can
// start or exit the thread as soon as it is runnable, so do not read fields
// from it after that publication merely to construct the return value.
uint64_t serviceCreateThreadWithArgument(void (*entry)(void*), uint64_t stack,
                                         void* argument) {
    auto* thread = Thread::createUser(
        task::getCurrent(), reinterpret_cast<uint64_t>(entry), stack,
        reinterpret_cast<uint64_t>(argument), 0, ~0ULL, false);
    if (!thread) return 0;
    const uint64_t id = thread->getId();
    task::enqueueThread(thread);
    return id;
}
// One-way endpoint send with real caller rights and a kernel-local message.
int serviceSend(uint64_t handle, os::IpcMessage& message, uint64_t deadline) {
    auto pin     = endpoint(handle, handleRightWrite);
    auto* target = pin.as<Endpoint>();
    return target ? target->send(&message, deadline)
                  : static_cast<int>(os::ErrorCode::InvalidHandle);
}
// Receive using current task rights and install transferred handles there.
int serviceReceive(uint64_t handle, os::IpcMessage& message,
                   uint64_t deadline) {
    auto pin     = endpoint(handle, handleRightRead);
    auto* target = pin.as<Endpoint>();
    return target ? target->receive(&message, deadline)
                  : static_cast<int>(os::ErrorCode::InvalidHandle);
}
// Use a supervisor-stack completion buffer shared across address spaces. An
// eventual reply can arrive while another CR3 is active; copying directly to
// private service globals or heap storage would target the wrong mapping.
int serviceCall(uint64_t handle, os::IpcMessage& request, os::IpcMessage& reply,
                uint64_t deadline) {
    auto pin     = endpoint(handle, handleRightWrite);
    auto* target = pin.as<Endpoint>();
    if (!target) return static_cast<int>(os::ErrorCode::InvalidHandle);
    // Another service completes the call under a different CR3. Only the
    // supervisor stack is shared; image globals and heap addresses are
    // private.
    os::IpcMessage completion{};
    const int result = target->call(&request, &completion, deadline);
    if (!result) reply = completion;
    return result;
}
// Attach the explicit token rather than relying on implicit per-thread RPC
// state; endpoint.reply validates the current claiming process.
int serviceReply(uint64_t handle, uint64_t token, os::IpcMessage& message) {
    auto pin           = endpoint(handle, handleRightRead);
    auto* target       = pin.as<Endpoint>();
    message.replyToken = token;
    return target ? target->reply(&message)
                  : static_cast<int>(os::ErrorCode::InvalidHandle);
}
// Cancel only a current-process thread's outstanding work on this endpoint.
int serviceCancel(uint64_t handle, uint64_t thread) {
    auto pin     = endpoint(handle, 0);
    auto* target = pin.as<Endpoint>();
    return target ? target->cancel(thread)
                  : static_cast<int>(os::ErrorCode::InvalidHandle);
}
// Monotonic nanoseconds for deadlines and elapsed-time calculations.
uint64_t serviceMonotonicTime() { return time::getMonotonicTime(); }
// Trusted text pointer to serial output; untrusted Ring3 logs use the bounded
// message parsing in systemKernelService instead.
void serviceLog(const char* text) {
    if (text) device::serialWrite(text);
}
// Return only task ID and the legacy trusted-device classification. This
// compatibility query is not a complete TaskInfo snapshot; normal user queries
// use the task service and its disclosure policy.
int serviceQueryTask(uint64_t id, os::wire::TaskInfo& info) {
    auto target = task::find(id);
    if (!target) return static_cast<int>(os::ErrorCode::NotFound);
    info                      = {};
    info.id                   = target->id();
    info.trustedDeviceService = target->isSystemService() &&
                                target->hasCapability(taskCapabilityDevice);
    return 0;
}
// Map a validated PCI BAR through the legacy trusted device gate, preserving
// resource-domain ownership instead of exposing arbitrary physical addresses.
uint64_t serviceMapMmio(const os::wire::PciFunctionInfo& function, uint8_t bar,
                        uint64_t address, uint64_t size) {
    if (!deviceAccess()) return 0;
    auto* region = device::MmioRegion::map(*task::getCurrent(), function, bar,
                                           address, size);
    const auto handle = region ? task::getCurrent()->handles().add(
                                     region, HandleType::MmioRegion, 3)
                               : 0;
    if (region && !handle) region->destroy();
    return handle;
}
// Allocate the default DMA buffer through a manage-capable device domain. info
// receives its addresses, size and installed buffer handle; allocation
// parameters follow DmaBuffer::create defaults in this compatibility entry.
int serviceAllocateDma(uint64_t device, uint64_t address,
                       os::wire::DmaBufferInfo& info) {
    if (!deviceAccess())
        return static_cast<int>(os::ErrorCode::PermissionDenied);
    HandleTable::Pin pin(task::getCurrent()->handles(), device,
                         HandleType::DeviceDomain, handleRightManage);
    auto* domain = pin.as<device::ResourceDomain>();
    if (!domain) return static_cast<int>(os::ErrorCode::InvalidHandle);
    auto* buffer =
        device::DmaBuffer::create(*task::getCurrent(), *domain, address, info);
    if (!buffer) return static_cast<int>(os::ErrorCode::OutOfMemory);
    info.handle =
        task::getCurrent()->handles().add(buffer, HandleType::DmaBuffer, 3);
    if (!info.handle) buffer->destroy();
    return info.handle ? 0 : static_cast<int>(os::ErrorCode::OutOfMemory);
}
// Require the trusted device gate and receive-capable endpoint, then install a
// managed IRQ binding handle. Interrupt delivery does not replace the
// device-specific acknowledge/unmask protocol.
uint64_t serviceBindIrq(uint8_t irq, uint64_t handle, bool levelTriggered) {
    auto pin     = endpoint(handle, handleRightRead);
    auto* target = pin.as<Endpoint>();
    if (!deviceAccess() || !target || irq > 15) return 0;
    auto* binding = device::InterruptBinding::create(*task::getCurrent(), irq,
                                                     target, levelTriggered);
    return binding
               ? task::getCurrent()->handles().add(
                     binding, HandleType::InterruptBinding, handleRightManage)
               : 0;
}
// Resolve the binding through the legacy borrowed lookup, then require the
// trusted device gate and binding owner for acknowledgment. This wrapper uses
// get rather than a retained Pin; it adds no independent lifetime guard.
int serviceAcknowledgeIrq(uint64_t handle) {
    auto* binding = static_cast<device::InterruptBinding*>(
        task::getCurrent()->handles().get(handle, HandleType::InterruptBinding,
                                          handleRightManage));
    return deviceAccess() && binding &&
                   binding->acknowledge(*task::getCurrent())
               ? 0
               : -1;
}

// Export only the exact mangled names listed below to immutable trusted
// images. Other serviceXxx definitions are not automatically exports, and
// ordinary Ring3 programs must not treat returned kernel addresses as APIs.
uint64_t serviceResolve(const char* symbol) {
    struct Import {
        const char* name;
        uint64_t address;
    };
    const Import imports[] = {
        {"_ZN2os17getStandardHandleEj",
         reinterpret_cast<uint64_t>(&serviceStandardHandle)},
        {"_ZN2os11unmapMemoryEmm",
         reinterpret_cast<uint64_t>(&serviceUnmapMemory)},
        {"_ZN2os13protectMemoryEmmm",
         reinterpret_cast<uint64_t>(&serviceProtectMemory)},
        {"_ZN2os11waitAddressEPVjj",
         reinterpret_cast<uint64_t>(&serviceWaitAddress)},
        {"_ZN2os11wakeAddressEPVjj",
         reinterpret_cast<uint64_t>(&serviceWakeAddress)},
        {"_ZN2os18getBootstrapHandleEv",
         reinterpret_cast<uint64_t>(&serviceBootstrapHandle)},
        {"_ZN2os11closeHandleEm",
         reinterpret_cast<uint64_t>(&serviceCloseHandle)},
        {"_ZN2os8exitTaskEi", reinterpret_cast<uint64_t>(&serviceExitTask)},
        {"_ZN2os10exitThreadEi",
         reinterpret_cast<uint64_t>(&serviceExitThread)},
        {"_ZN2os5yieldEv", reinterpret_cast<uint64_t>(&serviceYield)},
        {"_ZN2os5sleepEm", reinterpret_cast<uint64_t>(&serviceSleep)},
        {"_ZN2os9mapMemoryEmmm", reinterpret_cast<uint64_t>(&serviceMapMemory)},
        {"_ZN2os12createThreadEPFvvEm",
         reinterpret_cast<uint64_t>(&serviceCreateThread)},
        {"_ZN2os12createThreadEPFvPvEmS0_",
         reinterpret_cast<uint64_t>(&serviceCreateThreadWithArgument)},
        {"_ZN2os11sendMessageEmRNS_10IpcMessageEm",
         reinterpret_cast<uint64_t>(&serviceSend)},
        {"_ZN2os14receiveMessageEmRNS_10IpcMessageEm",
         reinterpret_cast<uint64_t>(&serviceReceive)},
        {"_ZN2os11callServiceEmRNS_10IpcMessageES1_m",
         reinterpret_cast<uint64_t>(&serviceCall)},
        {"_ZN2os12replyMessageEmmRNS_10IpcMessageE",
         reinterpret_cast<uint64_t>(&serviceReply)},
    };
    for (const auto& item : imports) {
        uint32_t i = 0;
        while (item.name[i] && symbol[i] == item.name[i]) ++i;
        if (!item.name[i] && !symbol[i]) return item.address;
    }
    return 0;
}

// Current-task page mapping compatibility wrapper; protection is decoded by
// the memory mechanism rather than trusted as arbitrary page-table bits.
uint64_t serviceMapMemory(uint64_t address, uint64_t size,
                          uint64_t protection) {
    auto* caller = task::getCurrent();
    return caller ? mm::mapUserPages(*caller, address, size, protection)
                  : static_cast<uint64_t>(-1);
}
// Remove pages belonging to the current task through normal memory rules.
bool serviceUnmapMemory(uint64_t address, uint64_t size) {
    auto* caller = task::getCurrent();
    return caller ? mm::unmapUserPages(*caller, address, size) == 0 : false;
}
// Apply permission changes through the current-task mapping mechanism.
bool serviceProtectMemory(uint64_t address, uint64_t size,
                          uint64_t protection) {
    auto* caller = task::getCurrent();
    return caller
               ? mm::protectUserPages(*caller, address, size, protection) == 0
               : false;
}

}  // namespace kernel::task
