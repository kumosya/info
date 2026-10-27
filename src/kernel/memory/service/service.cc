/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/service/service.cc
 * Resident memory-service request validation and mechanism dispatch.
 */

#include <new>

#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include <os/mm.h>

namespace kernel::mm {
namespace {

// Names are embedded message bytes, not raw user pointers. Require a nonempty
// terminator within both the transmitted payload and the 64-byte name limit.
bool readRequestName(const ::kernel::task::IpcMessage& request, uint32_t word,
                     const char*& name) {
    if (request.length <= word) return false;
    name = reinterpret_cast<const char*>(&request.data[word]);
    for (uint32_t i = 0; i < 64 && i < (request.length - word) * 8; ++i)
        if (!name[i]) return i != 0;
    return false;
}
// Serialize fixed-layout reply records without calling a user C library.
void copyRequestBytes(void* destination, const void* source, size_t size) {
    auto* output = static_cast<uint8_t*>(destination);
    auto* input  = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < size; ++i) output[i] = input[i];
}
// Cross-task requests require Memory role, capability and recovery ownership;
// zero or caller PID selects ordinary self-targeting.
bool requestAuthorized(::kernel::task::Task& caller, uint64_t target) {
    return !target || target == caller.id() ||
           (caller.serviceRole() == os::ServiceRole::Memory &&
            caller.hasCapability(::kernel::task::taskCapabilityMemory) &&
            ::kernel::task::serviceRecovery().current(caller));
}

}  // namespace
// Validate payloads before dispatching authenticated resident mechanisms.
// Business values occupy reply.data; native permission/dispatch failures
// return negative errors separately from successful message delivery.
int MemoryService::call(::kernel::task::Task& caller,
                        const ::kernel::task::IpcMessage& request,
                        ::kernel::task::IpcMessage& reply) {
    if (request.length < 5) return -1;
    const auto* args = request.data;
    const char* name = nullptr;
    switch (static_cast<os::MemoryOperation>(request.type)) {
        case os::MemoryOperation::Map:
            if (request.length < 6) return -1;
            reply.data[0] =
                map(args[0], args[1], args[2], args[3], args[4], args[5]);
            break;
        // Self-unmap or write removal cannot consume the synchronous reply
        // buffer: a successful destructive operation would otherwise destroy
        // its own return path before syscall completion can report the result.
        case os::MemoryOperation::Unmap:
        case os::MemoryOperation::Protect: {
            const bool protectOperation =
                request.type ==
                static_cast<uint64_t>(os::MemoryOperation::Protect);
            const uint64_t target = args[protectOperation ? 3 : 2];
            auto* context = ::kernel::task::currentThread()->syscallContext();
            // A synchronous request must keep its reply destination writable.
            if ((!target || target == caller.id()) && context && args[1] &&
                args[0] <= UINT64_MAX - args[1] &&
                context->rdx <=
                    UINT64_MAX - sizeof(::kernel::task::IpcMessage) &&
                args[0] < context->rdx + sizeof(::kernel::task::IpcMessage) &&
                context->rdx < args[0] + args[1] &&
                (!protectOperation || !(args[2] & 1)))
                return -1;
            reply.data[0] = protectOperation
                                ? protect(args[0], args[1], args[2], target)
                                : unmap(args[0], args[1], target);
            break;
        }

        // Pin a receive-capable endpoint through bind; PagerBridge further
        // checks its owner and the active Memory service identity before
        // retaining it.
        case os::MemoryOperation::BindPager: {
            ::kernel::task::HandleTable::Pin pin(
                caller.handles(), args[0], ::kernel::task::HandleType::Endpoint,
                ::kernel::task::handleRightRead);
            if (!pager_.bind(caller, pin.as<::kernel::task::Endpoint>()))
                return -3;
            break;
        }
        // Only current MM can freeze/thaw another process. Return its stable
        // root generation so recovered metadata can distinguish an intervening
        // exec.
        case os::MemoryOperation::Freeze:
        case os::MemoryOperation::Thaw: {
            if (caller.serviceRole() != os::ServiceRole::Memory ||
                !caller.hasCapability(::kernel::task::taskCapabilityMemory) ||
                !::kernel::task::serviceRecovery().current(caller))
                return -3;
            auto target = task::find(args[0]);
            if (!args[0] || !target || target->isTerminated()) return -4;
            if (!target->setMemoryFrozen(
                    request.type ==
                    static_cast<uint64_t>(os::MemoryOperation::Freeze)))
                return -8;
            ::kernel::task::PreemptionSpinLockGuard guard(
                target->addressSpace().lock());
            reply.data[1] = target->addressSpace().generation();
            break;
        }
        // A replacement pager enumerates by increasing PID to find clients
        // left frozen by its predecessor without relying on unstable list
        // positions.
        case os::MemoryOperation::FrozenNext: {
            if (caller.serviceRole() != os::ServiceRole::Memory ||
                !caller.hasCapability(::kernel::task::taskCapabilityMemory) ||
                !::kernel::task::serviceRecovery().current(caller))
                return -3;
            uint64_t cursor = args[0] ? args[0] : 1;
            while (true) {
                auto target = task::next(cursor);
                if (!target) {
                    reply.data[1] = 0;
                    break;
                }
                cursor = target->id() + 1;
                if (target->memoryFrozen() && !target->isTerminated()) {
                    reply.data[1] = target->id();
                    break;
                }
            }
            break;
        }
        // Require a frozen target and merge next object/shared intervals by
        // virtual start. External backing cannot be recovered by pretending
        // all PTEs own independent anonymous physical pages.
        case os::MemoryOperation::MappingNext: {
            if (caller.serviceRole() != os::ServiceRole::Memory ||
                !caller.hasCapability(::kernel::task::taskCapabilityMemory) ||
                !::kernel::task::serviceRecovery().current(caller))
                return -3;
            auto target = task::find(args[0]);
            if (!target || !target->memoryFrozen()) return -8;
            uint64_t pageAddress = 0, pageSize = 0, sharedAddress = 0,
                     sharedSize = 0;
            const bool page     = mappings_.nextManagedMapping(
                *target, args[1], pageAddress, pageSize);
            const bool shared = sharedMemory().nextManagedMapping(
                *target, args[1], sharedAddress, sharedSize);
            reply.data[1] = reply.data[2] = 0;
            if (page && (!shared || pageAddress < sharedAddress)) {
                reply.data[1] = pageAddress;
                reply.data[2] = pageSize;
            } else if (shared) {
                reply.data[1] = sharedAddress;
                reply.data[2] = sharedSize;
            }
            break;
        }
        // Expose selected state/permission/accessed/dirty bits, not the
        // physical frame address or arbitrary internal software bits from a
        // complete PTE.
        case os::MemoryOperation::PageState: {
            if (!requestAuthorized(caller, args[0])) return -3;
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target) return -4;
            ::kernel::task::PreemptionSpinLockGuard guard(
                target->addressSpace().lock());
            const auto root = target->addressSpace().rootPhysicalAddress();
            uint64_t flags  = 0;
            if (root &&
                mm::getPhysicalAddressInAddressSpace(root, args[1], &flags))
                reply.data[1] = flags & (pagePresent | pageWritable | pageUser |
                                         pageExternal | pageNoExecute |
                                         (1ULL << 5) | (1ULL << 6));
            break;
        }
        // Check a bounded aligned extent while holding the root lock. The
        // answer observes availability; it does not reserve that range after
        // unlocking.
        case os::MemoryOperation::RangeAvailable: {
            if (!requestAuthorized(caller, args[0])) return -3;
            if (!args[2] || ((args[1] | args[2]) & 4095) || args[1] < 0x10000 ||
                args[1] >= 0x0000800000000000ULL ||
                args[2] > 0x0000800000000000ULL - args[1])
                return -1;
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target || target->isTerminated()) return -4;
            ::kernel::task::PreemptionSpinLockGuard guard(
                target->addressSpace().lock());
            const auto root = target->addressSpace().rootPhysicalAddress();
            if (!root) return -4;
            reply.data[1] = 1;
            for (uint64_t offset = 0; offset < args[2]; offset += 4096)
                if (mm::getPhysicalAddressInAddressSpace(root,
                                                         args[1] + offset)) {
                    reply.data[1] = 0;
                    break;
                }
            break;
        }
        // Linked names, open handles and mapped views have separate lifetimes.
        // Created caller-local handles are advertised in the reply handle
        // vector.
        case os::MemoryOperation::Open:
            if (!readRequestName(request, 5, name) || args[0] > UINT32_MAX)
                return -1;
            reply.data[0] = sharedMemory().open(caller, name, args[0]);
            if (reply.data[0])
                reply.handles[reply.handleCount++] = reply.data[0];
            break;
        case os::MemoryOperation::Unlink:
            if (!readRequestName(request, 5, name)) return -1;
            reply.data[0] = sharedMemory().unlink(name) ? 0 : -1;
            break;
        case os::MemoryOperation::Resize:
            reply.data[0] =
                sharedMemory().resize(caller, args[0], args[1]) ? 0 : -1;
            break;
        case os::MemoryOperation::SharedInfo:
            if (!sharedMemory().querySize(caller, args[0], reply.data[1]))
                return -2;
            break;
        case os::MemoryOperation::UnlinkSharedHandle:
            if (!sharedMemory().unlink(caller, args[0])) return -4;
            break;
        // Create unlinked backing and resize before publishing its handle.
        // Close the only handle on failure so the unlinked unused object can
        // be reclaimed.
        case os::MemoryOperation::AnonymousShared: {
            if (!args[0] || args[0] > 64 * 4096) return -1;
            const uint64_t handle = sharedMemory().open(caller, nullptr, 0x42);
            if (!handle) return -5;
            if (!sharedMemory().resize(caller, handle, args[0])) {
                caller.handles().close(handle);
                return -5;
            }
            reply.data[0]                      = handle;
            reply.handles[reply.handleCount++] = handle;
            break;
        }
        // A wrapper alone is not a valid PageObject: require actual zeroed
        // backing before installing the handle. Roll back its reference on
        // installation failure instead of stranding an owned physical block.
        case os::MemoryOperation::AllocatePages: {
            if (args[0] > 12 || args[1] > UINT16_MAX ||
                !requestAuthorized(caller, args[2]))
                return -3;
            if (args[1] != anyMemoryNode &&
                args[1] >= memoryTopology().nodeCount())
                return -1;
            void* storage = mm::allocateHeap(sizeof(PageObject));
            if (!storage) return -5;
            auto* object = new (storage)
                PageObject(args[0], args[2] ? args[2] : caller.id(), args[1]);
            if (!object->valid()) {
                object->release();
                return -5;
            }
            const uint64_t handle = caller.handles().add(
                object, ::kernel::task::HandleType::PageObject,
                ::kernel::task::handleRightRead |
                    ::kernel::task::handleRightWrite |
                    ::kernel::task::handleRightManage);
            if (!handle) {
                object->release();
                return -5;
            }
            reply.data[0]                      = handle;
            reply.handles[reply.handleCount++] = handle;
            break;
        }
        // Pin while inspecting physical metadata so close cannot free backing
        // between reading object fields and constructing its wire record.
        case os::MemoryOperation::PageInfo: {
            ::kernel::task::HandleTable::Pin pin(
                caller.handles(), args[0],
                ::kernel::task::HandleType::PageObject,
                ::kernel::task::handleRightRead);
            auto* object = pin.as<PageObject>();
            if (!object) return -2;
            PhysicalPageInfo physical{};
            queryPhysicalPage(object->physical(), physical);
            const os::wire::PageObjectInfo info{
                object->owner(), object->size(), physical.getReferences(),
                physical.getNode(), static_cast<uint16_t>(object->order())};
            copyRequestBytes(&reply.data[1], &info, sizeof(info));
            break;
        }
        // Sample generation and ownership counters without promising a global
        // atomic snapshot. Owned physical bytes are not the sum of all virtual
        // aliases.
        case os::MemoryOperation::Usage: {
            if (!requestAuthorized(caller, args[0])) return -3;
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target || target->isTerminated()) return -4;
            uint64_t generation;
            {
                ::kernel::task::PreemptionSpinLockGuard guard(
                    target->addressSpace().lock());
                generation = target->addressSpace().generation();
            }
            const os::wire::MemoryUsage info{getTotalMemoryBytes(),
                                             getFreeMemoryBytes(),
                                             getOwnedMemoryBytes(target->id()),
                                             generation,
                                             memoryTopology().nodeCount(),
                                             0};
            copyRequestBytes(&reply.data[1], &info, sizeof(info));
            break;
        }
        // Draining shared kernel caches requires explicit Memory capability
        // even though this operation does not directly edit any user mapping.
        case os::MemoryOperation::ReclaimCaches:
            if (!caller.hasCapability(::kernel::task::taskCapabilityMemory))
                return -3;
            reply.data[0] = mm::reclaimCaches();
            break;
        default:
            return -1;
    }
    reply.length = os::ipcDataWordCount;
    return 0;
}
}  // namespace kernel::mm

namespace kernel::task {
namespace {
// Bind shared registries but keep the real caller identity. A separate worker
// must not impersonate this task's memory or saved execution context.
int dispatchMemory(Task& caller, const IpcMessage& request, IpcMessage& reply) {
    static mm::MemoryService implementation(mm::maps(), mm::pagerBridge());
    return implementation.call(caller, request, reply);
}
}  // namespace
// Concrete resident dispatch; user-space VMA/paging policy is reached through
// the independently authenticated PagerBridge binding when needed.
EndpointService& memoryKernelService() {
    static EndpointService service(dispatchMemory);
    return service;
}
}  // namespace kernel::task
