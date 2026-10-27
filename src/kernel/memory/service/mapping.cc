/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/service/mapping.cc
 * Caller-authorized mapping mechanisms for pages and memory objects.
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include <os/device.h>

namespace kernel::mm {

namespace {
// Native negative mapping error in the unsigned result field.
uint64_t error(::kernel::task::SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
}

// Cross-process authority requires role, capability and current recovery
// ownership together, not mere possession of a resident service endpoint.
bool isMemoryManager(const ::kernel::task::Task& caller) {
    return caller.serviceRole() == os::ServiceRole::Memory &&
           caller.hasCapability(::kernel::task::taskCapabilityMemory) &&
           ::kernel::task::serviceRecovery().current(caller);
}
// Ordinary callers target self; the current trusted pager may target others.
bool mappingAuthorized(const ::kernel::task::Task& caller, uint64_t target) {
    return !target || target == caller.id() || isMemoryManager(caller);
}

}  // namespace
// Retain the target but pin source handles in the actual caller table. Decode
// write/execute independently of mapping kind; anonymous, PageObject and
// shared backing have different ownership and rollback responsibilities.
uint64_t MemoryService::map(uint64_t address, uint64_t size, uint64_t flags,
                            uint64_t handle, uint64_t offset,
                            uint64_t targetId) {
    auto* caller = task::getCurrent();
    if (!caller || !mappingAuthorized(*caller, targetId))
        return error(::kernel::task::SyscallError::PermissionDenied);
    auto target = task::find(targetId ? targetId : caller->id());
    if (!target || target->isTerminated())
        return error(::kernel::task::SyscallError::NotFound);
    const uint64_t protection = flags & 3;
    switch (flags & ~3ULL) {
        case OS_MAP_ANONYMOUS:
            if (handle || offset)
                return error(::kernel::task::SyscallError::InvalidArgument);
            return mm::mapUserPages(*target, address, size, protection);
        // Derive future protection ceiling from original handle rights, so a
        // read-only mapping cannot later invent write authority by changing
        // its PTE.
        case OS_MAP_PAGE_OBJECT: {
            if (protection == 3)
                return error(::kernel::task::SyscallError::PermissionDenied);
            const uint32_t required =
                ::kernel::task::handleRightRead |
                ((protection & 1) ? ::kernel::task::handleRightWrite : 0);
            ::kernel::task::HandleTable::Pin pin(
                caller->handles(), handle,
                ::kernel::task::HandleType::PageObject, required);
            auto* object = pin.as<mm::PageObject>();
            if (!object)
                return error(::kernel::task::SyscallError::InvalidHandle);
            void* inspected                 = nullptr;
            ::kernel::task::HandleType type = ::kernel::task::HandleType::None;
            uint32_t rights                 = 0;
            if (!caller->handles().inspect(handle, &inspected, &type,
                                           &rights) ||
                inspected != object ||
                type != ::kernel::task::HandleType::PageObject)
                return error(::kernel::task::SyscallError::InvalidHandle);
            const uint64_t maximum =
                2 | ((rights & ::kernel::task::handleRightWrite) ? 1 : 0);
            return mappings_.map(*target, *object, address, size, offset,
                                 protection, maximum)
                       ? address
                       : error(::kernel::task::SyscallError::InvalidArgument);
        }
        // Use a temporary target-local handle for cross-task mapping
        // authorization. Close it after setup; a successful mapping has its
        // own object lifetime independent of this temporary handle slot.
        case OS_MAP_SHARED_OBJECT: {
            if (target == caller)
                return sharedMemory().map(*caller, handle, address, size,
                                          offset, protection);
            const uint64_t copied =
                sharedMemory().duplicateHandle(*caller, *target, handle);
            if (!copied)
                return error(::kernel::task::SyscallError::InvalidHandle);
            const uint64_t result = sharedMemory().map(
                *target, copied, address, size, offset, protection);
            target->handles().close(copied);
            return result;
        }
        default:
            return error(::kernel::task::SyscallError::InvalidArgument);
    }
}
// Authorize and retain before range unmapping; its address-space lock and
// split preparation live in the common unmapUserPages implementation.
uint64_t MemoryService::unmap(uint64_t address, uint64_t size,
                              uint64_t targetId) {
    auto* caller = task::getCurrent();
    if (!caller || !mappingAuthorized(*caller, targetId))
        return error(::kernel::task::SyscallError::PermissionDenied);
    auto target = task::find(targetId ? targetId : caller->id());
    return !target || target->isTerminated()
               ? error(::kernel::task::SyscallError::NotFound)
               : mm::unmapUserPages(*target, address, size);
}
// Only current MM may establish COW. Ordinary protection must obey recorded
// backing rights and cannot directly re-enable a COW mapping's write bit.
uint64_t MemoryService::protect(uint64_t address, uint64_t size,
                                uint64_t protection, uint64_t targetId) {
    auto* caller = task::getCurrent();
    if (!caller || !mappingAuthorized(*caller, targetId))
        return error(::kernel::task::SyscallError::PermissionDenied);
    auto target = task::find(targetId ? targetId : caller->id());
    if (!target || target->isTerminated())
        return error(::kernel::task::SyscallError::NotFound);
    if (protection == OS_PROTECT_COPY_ON_WRITE) {
        if (!isMemoryManager(*caller))
            return error(::kernel::task::SyscallError::PermissionDenied);
        ::kernel::task::PreemptionSpinLockGuard guard(
            target->addressSpace().lock());
        return mappings_.markCopyOnWrite(*target, address, size)
                   ? 0
                   : error(::kernel::task::SyscallError::InvalidArgument);
    }
    return mm::protectUserPages(*target, address, size, protection);
}

}  // namespace kernel::mm
