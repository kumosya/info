#include "kernel/services.h"

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/serial.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "kernel/time.h"
#include "kernel/core_service.h"
#include "kernel/sync.h"

namespace kernel {
namespace {
HandleTable::Pin endpoint(uint64_t handle, uint32_t rights) {
    return HandleTable::Pin(task::getCurrent()->handles(), handle, HandleType::Endpoint, rights);
}
bool deviceAccess() {
    auto* task = task::getCurrent();
    return task && task->isSystemService() && task->hasCapability(taskCapabilityDevice);
}
}

uint64_t Services::coreHandle(os::CoreServiceKind kind) { return CoreServices::getHandle(*task::getCurrent(), kind); }
uint64_t Services::standardHandle(uint32_t index) { return task::getCurrent()->standardHandle(index); }
uint64_t Services::threadId() { return Thread::getCurrent()->getId(); }
int Services::waitAddress(volatile uint32_t* address, uint32_t expected) {
    if ((reinterpret_cast<uint64_t>(address) & 3) != 0) return -1;
    const int result = sync::waitOnUserAddress(address, expected);
    return result < 0 ? -1 : result > 0 ? -6 : 0;
}
int Services::wakeAddress(volatile uint32_t* address, uint32_t count) {
    return (reinterpret_cast<uint64_t>(address) & 3) ? -1 : sync::wakeUserAddress(address, count);
}
int64_t Services::duplicateHandle(uint64_t handle, uint32_t rights, uint32_t flags) {
    const uint64_t result = task::getCurrent()->handles().duplicate(handle, rights, flags);
    return result ? static_cast<int64_t>(result) : -1;
}
int Services::restrictHandle(uint64_t handle, uint32_t rights) { return task::getCurrent()->handles().restrictRights(handle, rights) ? 0 : -3; }
int Services::setHandleFlags(uint64_t handle, uint32_t flags) { return task::getCurrent()->handles().setFlags(handle, flags) ? 0 : -1; }
int Services::handleState(uint64_t handle) {
    auto pin = endpoint(handle, 0);
    auto* value = pin.as<Endpoint>();
    return value ? (value->isClosed() ? 1 : 0) : -2;
}
uint64_t Services::createEndpoint() {
    auto* value = Endpoint::create();
    return value ? task::getCurrent()->handles().add(value, HandleType::Endpoint, 7) : 0;
}
uint64_t Services::bootstrapHandle() { return task::getCurrent()->bootstrapHandle(); }
bool Services::closeHandle(uint64_t handle) { return task::getCurrent()->handles().close(handle); }
void Services::exitTask(int code) { task::exitCurrent(code); }
void Services::exitThread(int code) { Thread::exitCurrent(code); }
void Services::yield() { schedule::yieldThread(); }
void Services::sleep(uint64_t milliseconds) { Thread::sleepCurrent(milliseconds); }
uint64_t Services::createThread(void (*entry)(), uint64_t stack) {
    return createThreadWithArgument(reinterpret_cast<void (*)(void*)>(entry), stack, nullptr);
}
uint64_t Services::createThreadWithArgument(void (*entry)(void*), uint64_t stack, void* argument) {
    auto* thread = Thread::createUser(task::getCurrent(), reinterpret_cast<uint64_t>(entry),
        stack, reinterpret_cast<uint64_t>(argument), 0, ~0ULL, false);
    if (!thread) return 0;
    const uint64_t id = thread->getId();
    schedule::enqueueThread(thread);
    return id;
}
int Services::send(uint64_t handle, os::IpcMessage& message, uint64_t deadline) {
    auto pin = endpoint(handle, handleRightWrite);
    auto* target = pin.as<Endpoint>();
    return target ? target->send(&message, deadline) : static_cast<int>(os::ErrorCode::InvalidHandle);
}
int Services::receive(uint64_t handle, os::IpcMessage& message, uint64_t deadline) {
    auto pin = endpoint(handle, handleRightRead);
    auto* target = pin.as<Endpoint>();
    return target ? target->receive(&message, deadline) : static_cast<int>(os::ErrorCode::InvalidHandle);
}
int Services::call(uint64_t handle, os::IpcMessage& request, os::IpcMessage& reply, uint64_t deadline) {
    auto pin = endpoint(handle, handleRightWrite);
    auto* target = pin.as<Endpoint>();
    if (!target) return static_cast<int>(os::ErrorCode::InvalidHandle);
    // Another service completes the call under a different CR3. Only the
    // supervisor stack is shared; image globals and heap addresses are private.
    os::IpcMessage completion{};
    const int result = target->call(&request, &completion, deadline);
    if (!result) reply = completion;
    return result;
}
int Services::reply(uint64_t handle, uint64_t token, os::IpcMessage& message) {
    auto pin = endpoint(handle, handleRightRead);
    auto* target = pin.as<Endpoint>();
    message.replyToken = token;
    return target ? target->reply(&message) : static_cast<int>(os::ErrorCode::InvalidHandle);
}
int Services::cancel(uint64_t handle, uint64_t thread) {
    auto pin = endpoint(handle, 0);
    auto* target = pin.as<Endpoint>();
    return target ? target->cancel(thread) : static_cast<int>(os::ErrorCode::InvalidHandle);
}
uint64_t Services::monotonicTime() { return time::getMonotonicTime(); }
void Services::log(const char* text) { if (text) serial::serialWrite(text); }
int Services::pciRequest(os::PciRequest& request) {
    auto* caller = task::getCurrent();
    if (!caller || !caller->isSystemService() || !caller->hasCapability(taskCapabilityPci))
        return static_cast<int>(os::ErrorCode::PermissionDenied);
    switch (request.operation) {
        case os::PciOperation::Enumerate:
            return request.functions && request.capacity && request.capacity <= 64
                ? device::PciAccess::enumerate(request.functions, request.capacity) : -1;
        case os::PciOperation::ReadConfig:
            return device::PciAccess::read(request.function, request.offset, request.value) ? 0 : -1;
        case os::PciOperation::WriteConfig:
            return device::ResourceDomain::writeConfiguration(request.requester, request.function, request.offset, request.value) ? 0 : -1;
    }
    return -1;
}
int Services::queryTask(uint64_t id, os::TaskInfo& info) {
    auto target = task::find(id);
    if (!target) return static_cast<int>(os::ErrorCode::NotFound);
    info = {};
    info.id = target->id();
    info.trustedDeviceService = target->isSystemService() && target->hasCapability(taskCapabilityDevice);
    return 0;
}
uint64_t Services::mapMmio(const os::PciFunctionInfo& function, uint8_t bar, uint64_t address, uint64_t size) {
    if (!deviceAccess()) return 0;
    auto* region = device::MmioRegion::map(*task::getCurrent(), function, bar, address, size);
    const auto handle = region ? task::getCurrent()->handles().add(region, HandleType::MmioRegion, 3) : 0;
    if (region && !handle) region->destroy();
    return handle;
}
int Services::allocateDma(uint64_t device, uint64_t address, os::DmaBufferInfo& info) {
    if (!deviceAccess()) return static_cast<int>(os::ErrorCode::PermissionDenied);
    HandleTable::Pin pin(task::getCurrent()->handles(), device, HandleType::DeviceDomain, handleRightManage);
    auto* domain = pin.as<device::ResourceDomain>();
    if (!domain) return static_cast<int>(os::ErrorCode::InvalidHandle);
    auto* buffer = device::DmaBuffer::create(*task::getCurrent(), *domain, address, info);
    if (!buffer) return static_cast<int>(os::ErrorCode::OutOfMemory);
    info.handle = task::getCurrent()->handles().add(buffer, HandleType::DmaBuffer, 3);
    if (!info.handle) buffer->destroy();
    return info.handle ? 0 : static_cast<int>(os::ErrorCode::OutOfMemory);
}
uint64_t Services::bindIrq(uint8_t irq, uint64_t handle, bool levelTriggered) {
    auto pin = endpoint(handle, handleRightRead);
    auto* target = pin.as<Endpoint>();
    if (!deviceAccess() || !target || irq > 15) return 0;
    auto* binding = device::InterruptBinding::create(*task::getCurrent(), irq, target, levelTriggered);
    return binding ? task::getCurrent()->handles().add(binding, HandleType::InterruptBinding, handleRightManage) : 0;
}
int Services::acknowledgeIrq(uint64_t handle) {
    auto* binding = static_cast<device::InterruptBinding*>(task::getCurrent()->handles().get(
        handle, HandleType::InterruptBinding, handleRightManage));
    return deviceAccess() && binding && binding->acknowledge(*task::getCurrent()) ? 0 : -1;
}

uint64_t Services::resolve(const char* symbol) {
    struct Import { const char* name; uint64_t address; };
    const Import imports[] = {
        {"_ZN2os17CoreServiceClient10openHandleENS_15CoreServiceKindE", reinterpret_cast<uint64_t>(&coreHandle)},
        {"_ZN2os17getStandardHandleEj", reinterpret_cast<uint64_t>(&standardHandle)},
        {"_ZN2os4Task11getThreadIdEv", reinterpret_cast<uint64_t>(&threadId)},
        {"_ZN2os11unmapMemoryEmm", reinterpret_cast<uint64_t>(&unmapMemory)},
        {"_ZN2os13protectMemoryEmmm", reinterpret_cast<uint64_t>(&protectMemory)},
        {"_ZN2os11waitAddressEPVjj", reinterpret_cast<uint64_t>(&waitAddress)},
        {"_ZN2os11wakeAddressEPVjj", reinterpret_cast<uint64_t>(&wakeAddress)},
        {"_ZN2os6Handle9duplicateEmjj", reinterpret_cast<uint64_t>(&duplicateHandle)},
        {"_ZN2os6Handle14restrictRightsEmj", reinterpret_cast<uint64_t>(&restrictHandle)},
        {"_ZN2os6Handle8setFlagsEmj", reinterpret_cast<uint64_t>(&setHandleFlags)},
        {"_ZN2os6Handle5stateEm", reinterpret_cast<uint64_t>(&handleState)},
        {"_ZN2os8Endpoint6createEv", reinterpret_cast<uint64_t>(&createEndpoint)},
        {"_ZN2os18getBootstrapHandleEv", reinterpret_cast<uint64_t>(&bootstrapHandle)},
        {"_ZN2os11closeHandleEm", reinterpret_cast<uint64_t>(&closeHandle)},
        {"_ZN2os8exitTaskEi", reinterpret_cast<uint64_t>(&exitTask)},
        {"_ZN2os10exitThreadEi", reinterpret_cast<uint64_t>(&exitThread)},
        {"_ZN2os5yieldEv", reinterpret_cast<uint64_t>(&yield)},
        {"_ZN2os5sleepEm", reinterpret_cast<uint64_t>(&sleep)},
        {"_ZN2os9mapMemoryEmmm", reinterpret_cast<uint64_t>(&mapMemory)},
        {"_ZN2os12createThreadEPFvvEm", reinterpret_cast<uint64_t>(&createThread)},
        {"_ZN2os12createThreadEPFvPvEmS0_", reinterpret_cast<uint64_t>(&createThreadWithArgument)},
        {"_ZN2os11sendMessageEmRNS_10IpcMessageEm", reinterpret_cast<uint64_t>(&send)},
        {"_ZN2os14receiveMessageEmRNS_10IpcMessageEm", reinterpret_cast<uint64_t>(&receive)},
        {"_ZN2os11callServiceEmRNS_10IpcMessageES1_m", reinterpret_cast<uint64_t>(&call)},
        {"_ZN2os12replyMessageEmmRNS_10IpcMessageE", reinterpret_cast<uint64_t>(&reply)},
        {"_ZN2os8Endpoint6cancelEmm", reinterpret_cast<uint64_t>(&cancel)},
        {"_ZN2os5Clock16getMonotonicTimeEv", reinterpret_cast<uint64_t>(&monotonicTime)},
        {"_ZN2os6System3logEPKc", reinterpret_cast<uint64_t>(&log)},
        {"_ZN2os13SystemService10pciRequestERNS_10PciRequestE", reinterpret_cast<uint64_t>(&pciRequest)},
        {"_ZN2os13SystemService9queryTaskEmRNS_8TaskInfoE", reinterpret_cast<uint64_t>(&queryTask)},
        {"_ZN2os6Device13mapDeviceMmioERKNS_15PciFunctionInfoEhmm", reinterpret_cast<uint64_t>(&mapMmio)},
        {"_ZN2os6Device11allocateDmaEmmRNS_13DmaBufferInfoE", reinterpret_cast<uint64_t>(&allocateDma)},
        {"_ZN2os6Device7bindIrqEhmb", reinterpret_cast<uint64_t>(&bindIrq)},
        {"_ZN2os6Device14acknowledgeIrqEm", reinterpret_cast<uint64_t>(&acknowledgeIrq)}
    };
    for (const auto& item : imports) {
        uint32_t i = 0;
        while (item.name[i] && symbol[i] == item.name[i]) ++i;
        if (!item.name[i] && !symbol[i]) return item.address;
    }
    return 0;
}
}  // namespace kernel
