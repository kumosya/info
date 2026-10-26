#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "kernel/mm.h"
#include "kernel/ipc.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "kernel/page.h"
#include "service/common/requests.h"
namespace kernel::memory {
namespace {
SpinLock pagerLock; Endpoint* pagerEndpoint;
int exchange(IpcMessage& request, IpcMessage& reply, bool optional = false) {
    const auto flags = interrupt::disable(); interrupt::enable();
    const uint64_t deadline = time::getMonotonicTime() + 20000000000ULL;
    bool observed = false;
    while (true) {
        Endpoint* endpoint;
        {
            InterruptSpinLockGuard guard(pagerLock); endpoint = pagerEndpoint;
            if (endpoint) { endpoint->retain(); observed = true; }
        }
        if (!endpoint && !observed) { interrupt::restore(flags); return optional ? 0 : static_cast<int>(os::ErrorCode::PeerDied); }
        const int result = endpoint ? endpoint->callFromKernel(&request, &reply, deadline) : static_cast<int>(os::ErrorCode::PeerDied);
        if (endpoint) endpoint->release();
        if (result != static_cast<int>(os::ErrorCode::PeerDied) && result != static_cast<int>(os::ErrorCode::EndpointClosed)) {
            interrupt::restore(flags); return result;
        }
        if (time::getMonotonicTime() >= deadline) { interrupt::restore(flags); return static_cast<int>(os::ErrorCode::Timeout); }
        Thread::sleepCurrent(1);
    }
}
}
bool PagerBridge::bind(Task& owner, Endpoint* endpoint) {
    if (!endpoint || !owner.hasCapability(taskCapabilityMemory) || owner.serviceRole() != os::ServiceRole::Memory || !ServiceRecovery::current(owner) || endpoint->getOwner() != &owner) return false;
    InterruptSpinLockGuard guard(pagerLock);
    if (pagerEndpoint && !pagerEndpoint->isClosed() && pagerEndpoint->getOwner() != &owner) return false;
    endpoint->retain(); if (pagerEndpoint) pagerEndpoint->release(); pagerEndpoint = endpoint; return true;
}
bool PagerBridge::resolve(uint64_t address, uint64_t flags, int* failure) {
    if (failure) *failure = -4;
    Task* caller = task::getCurrent();
    if (!caller || caller->serviceRole() == os::ServiceRole::Memory || address < 0x10000 || address >= 0x0000800000000000ULL || (flags & 8)) return false;
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(os::MmOperation::Fault); request.length = 3;
    request.data[0] = address; request.data[1] = flags;
    const int result = exchange(request, reply);
    if (result || static_cast<int64_t>(reply.data[0])) {
        if (failure) *failure = result ? result : static_cast<int>(reply.data[0]);
        return false;
    }
    PreemptionSpinLockGuard guard(caller->addressSpace().lock());
    const uint64_t root = caller->addressSpace().rootPhysicalAddress(); uint64_t actual = 0;
    return root && memory::validateUserMemory(root, address, 1, (flags & 2) != 0) &&
        memory::getPhysicalAddressInAddressSpace(root, address, &actual) && (!(flags & 16) || !(actual & pageNoExecute));
}
bool PagerBridge::clone(Task& parent, Task& child) {
    if (parent.serviceRole() == os::ServiceRole::Memory) return false;
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(os::MmOperation::Fork); request.length = 1;
    request.data[0] = child.id();
    const int result = exchange(request, reply, true);
    return !result && static_cast<int64_t>(reply.data[0]) == 0;
}
bool PagerBridge::ensureUserMemory(uint64_t address, uint64_t size, bool writable) {
    Task* caller = task::getCurrent();
    if (!caller) return false;
    if (!size) return true;
    if (address < 0x10000 || address >= 0x0000800000000000ULL || size > 0x0000800000000000ULL - address) return false;
    const uint64_t last = (address + size - 1) & ~4095ULL;
    for (uint64_t page = address & ~4095ULL;; page += 4096) {
        bool valid;
        {
            PreemptionSpinLockGuard guard(caller->addressSpace().lock());
            valid = memory::validateUserMemory(caller->addressSpace().rootPhysicalAddress(), page, 1, writable);
        }
        if (!valid && !resolve(page, writable ? 2 : 0)) return false;
        if (page == last) return true;
    }
}
}  // namespace kernel::memory
