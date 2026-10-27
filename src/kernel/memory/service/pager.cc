/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/service/pager.cc
 * Authenticated bridge to the user-space memory policy and pager service.
 */

#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"

namespace kernel::mm {

namespace {
PagerBridge pager;
}
// Shared retained endpoint for the active user-space memory policy service.
PagerBridge& pagerBridge() { return pager; }
// Retain the binding while calling, but never hold pagerLock across RPC. Allow
// interrupts for waits and retry replacement within one absolute twenty-second
// budget. Restore the caller's original interrupt state. optional permits
// initial bootstrap absence, not silent success after a previously observed
// service disappears.
int PagerBridge::exchange(::kernel::task::IpcMessage& request,
                          ::kernel::task::IpcMessage& reply, bool optional) {
    const auto flags = cpu::disableInterrupts();
    cpu::enableInterrupts();
    const uint64_t deadline = time::getMonotonicTime() + 20000000000ULL;
    bool observed           = false;
    while (true) {
        ::kernel::task::Endpoint* endpoint;
        {
            ::kernel::task::InterruptSpinLockGuard guard(pagerLock);
            endpoint = pagerEndpoint;
            if (endpoint) {
                endpoint->retain();
                observed = true;
            }
        }
        // Before any binding was observed, absence can be normal bootstrap.
        // After an observation, missing/dead binding means recovery in
        // progress; retry only within the original deadline rather than
        // extending it on every failure.
        if (!endpoint && !observed) {
            cpu::restoreInterrupts(flags);
            return optional ? 0 : static_cast<int>(os::ErrorCode::PeerDied);
        }
        const int result =
            endpoint ? endpoint->callFromKernel(&request, &reply, deadline)
                     : static_cast<int>(os::ErrorCode::PeerDied);
        if (endpoint) endpoint->release();
        if (result != static_cast<int>(os::ErrorCode::PeerDied) &&
            result != static_cast<int>(os::ErrorCode::EndpointClosed)) {
            cpu::restoreInterrupts(flags);
            return result;
        }
        if (time::getMonotonicTime() >= deadline) {
            cpu::restoreInterrupts(flags);
            return static_cast<int>(os::ErrorCode::Timeout);
        }
        ::kernel::task::sleepCurrentThread(1);
    }
}
// Require Memory role, capability and current recovery-domain ownership. The
// endpoint must belong to that instance; receiving some unrelated endpoint
// handle cannot replace a live pager from another instance.
bool PagerBridge::bind(::kernel::task::Task& owner,
                       ::kernel::task::Endpoint* endpoint) {
    if (!endpoint ||
        !owner.hasCapability(::kernel::task::taskCapabilityMemory) ||
        owner.serviceRole() != os::ServiceRole::Memory ||
        !::kernel::task::serviceRecovery().current(owner) ||
        endpoint->getOwner() != &owner)
        return false;
    ::kernel::task::InterruptSpinLockGuard guard(pagerLock);
    if (pagerEndpoint && !pagerEndpoint->isClosed() &&
        pagerEndpoint->getOwner() != &owner)
        return false;
    endpoint->retain();
    if (pagerEndpoint) pagerEndpoint->release();
    pagerEndpoint = endpoint;
    return true;
}
// Reject recursive pager-self faults and reserved-bit table corruption. After
// RPC success, recheck actual user/write/execute mapping under the root lock;
// a success reply alone is not proof hardware can retry the access.
bool PagerBridge::resolve(uint64_t address, uint64_t flags, int* failure) {
    if (failure) *failure = -4;
    ::kernel::task::Task* caller = task::getCurrent();
    if (!caller || caller->serviceRole() == os::ServiceRole::Memory ||
        address < 0x10000 || address >= 0x0000800000000000ULL || (flags & 8))
        return false;
    ::kernel::task::IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(os::MmOperation::Fault);
    request.length   = 3;
    request.data[0]  = address;
    request.data[1]  = flags;
    const int result = exchange(request, reply);
    if (result || static_cast<int64_t>(reply.data[0])) {
        if (failure)
            *failure = result ? result : static_cast<int>(reply.data[0]);
        return false;
    }
    ::kernel::task::PreemptionSpinLockGuard guard(
        caller->addressSpace().lock());
    const uint64_t root = caller->addressSpace().rootPhysicalAddress();
    uint64_t actual     = 0;
    return root && mm::validateUserMemory(root, address, 1, (flags & 2) != 0) &&
           mm::getPhysicalAddressInAddressSpace(root, address, &actual) &&
           (!(flags & 16) || !(actual & pageNoExecute));
}
// Synchronize child VMA/COW policy before fork publishes a runnable thread.
// The Memory service cannot recursively clone itself through its own pager.
bool PagerBridge::clone(::kernel::task::Task& parent,
                        ::kernel::task::Task& child) {
    if (parent.serviceRole() == os::ServiceRole::Memory) return false;
    ::kernel::task::IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(os::MmOperation::Fork);
    request.length   = 1;
    request.data[0]  = child.id();
    const int result = exchange(request, reply, true);
    return !result && static_cast<int64_t>(reply.data[0]) == 0;
}
// Validate page by page with short root-lock scopes. Drop the address-space
// lock before resolve: the pager needs to map into that target to satisfy the
// fault, so holding this lock across its RPC would deadlock.
bool PagerBridge::ensureUserMemory(uint64_t address, uint64_t size,
                                   bool writable) {
    ::kernel::task::Task* caller = task::getCurrent();
    if (!caller) return false;
    if (!size) return true;
    if (address < 0x10000 || address >= 0x0000800000000000ULL ||
        size > 0x0000800000000000ULL - address)
        return false;
    const uint64_t last = (address + size - 1) & ~4095ULL;
    for (uint64_t page = address & ~4095ULL;; page += 4096) {
        bool valid;
        {
            ::kernel::task::PreemptionSpinLockGuard guard(
                caller->addressSpace().lock());
            valid = mm::validateUserMemory(
                caller->addressSpace().rootPhysicalAddress(), page, 1,
                writable);
        }
        if (!valid && !resolve(page, writable ? 2 : 0)) return false;
        if (page == last) return true;
    }
}

}  // namespace kernel::mm
