/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/virtual/page_mappings.cc
 * Page-object mapping references and pager-managed address intervals.
 */

#include <new>

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"

namespace kernel::mm {

namespace {
Maps mappingRegistry;
}
// Shared registry of PageObject virtual intervals and retained backing.
Maps& maps() { return mappingRegistry; }
// Construct a record from bounded storage rather than an unbounded heap path.
Map* Maps::allocate() {
    void* value = pool.allocate();
    return value ? new (value) Map{} : nullptr;
}
// Initialize record pool/list before core memory endpoints become usable.
bool Maps::init() {
    lock.init();
    mappings = nullptr;
    return pool.init(storage, sizeof(storage), sizeof(Map));
}
// Only known pager-managed RAM exempts pageExternal from core exclusion. A
// device translation alone must never authorize snapshot reads.
bool Maps::dumpAllowed(::kernel::task::Task& target, uint64_t address) {
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    for (const Map* r = mappings; r; r = r->next)
        if (r->target == &target && r->pagerManaged && address >= r->address &&
            address - r->address < r->size)
            return true;
    return false;
}
// Lock address space before registry, validate range/ceiling/overlap, then
// reserve a record and install borrowed PTEs. Publish and retain only after
// all leaves succeed; rollback removes its prefix and unused record.
bool Maps::map(::kernel::task::Task& target, PageObject& object,
               uint64_t address, uint64_t size, uint64_t offset,
               uint64_t protection, uint64_t maximumProtection) {
    if (!size || ((address | size | offset) & 4095) || address < 0x10000 ||
        address + size <= address || address + size > 0x0000800000000000ULL ||
        offset > object.size() || size > object.size() - offset ||
        protection > 2 || maximumProtection > 3 ||
        (protection & ~maximumProtection))
        return false;
    ::kernel::task::PreemptionSpinLockGuard space(target.addressSpace().lock());
    if (!target.addressSpace().rootPhysicalAddress()) return false;
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    for (Map* r = mappings; r; r = r->next)
        if (r->target == &target && r->overlaps(address, size)) return false;
    Map* record = allocate();
    if (!record) return false;
    const uint64_t root  = target.addressSpace().rootPhysicalAddress();
    uint64_t flags = pageUser | pageExternal |
                           ((protection & 1) ? pageWritable : 0) |
                           ((protection & 2) ? 0 : pageNoExecute);
    auto residency=target.addressSpace().residency();
    const bool pager=kernel::task::getCurrent() && kernel::task::getCurrent()!=&target &&
        kernel::task::getCurrent()->serviceRole()==os::ServiceRole::Memory;
    if(!pager && target.addressSpace().lockFuture() &&
       (!residency.set(address,size,true) || residency.bytes>target.memlockLimit())){pool.release(record);return false;}
    uint64_t mapped = 0;
    while (mapped < size && mm::mapPageInAddressSpace(
                                root, address + mapped,
                                object.physical() + offset + mapped, flags | (residency.contains(address+mapped)?pageLocked:0)))
        mapped += 4096;
    if (mapped != size) {
        while (mapped) {
            mapped -= 4096;
            mm::unmapPageInAddressSpace(root, address + mapped);
        }
        pool.release(record);
        return false;
    }
    target.addressSpace().residency()=residency;
    object.retain();
    *record = {&target, &object,           address, size,
               offset,  maximumProtection, mappings};
    // Authenticate mapping provenance from the executing Memory service; it is
    // not a caller-supplied flag trusted by recovery or core.
    record->pagerManaged =
        task::getCurrent() && task::getCurrent() != &target &&
        task::getCurrent()->serviceRole() == os::ServiceRole::Memory;
    mappings = record;
    return true;
}
// Select the lowest managed interval at or beyond cursor. A consistent
// multi-call recovery walk requires the caller to coordinate a frozen target.
bool Maps::nextManagedMapping(::kernel::task::Task& target, uint64_t cursor,
                              uint64_t& address, uint64_t& size) {
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    const Map* selected = nullptr;
    for (const Map* r = mappings; r; r = r->next)
        if (r->target == &target && r->pagerManaged && r->address >= cursor &&
            (!selected || r->address < selected->address))
            selected = r;
    if (!selected) return false;
    address = selected->address;
    size    = selected->size;
    return true;
}
// A strict middle hole leaves two survivors. Reserve the second record before
// removing PTEs while the caller stabilizes the target address space.
bool Maps::prepareUnmap(::kernel::task::Task& target, uint64_t address,
                        uint64_t size, void*& reservation) {
    reservation = nullptr;
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    for (Map* r = mappings; r; r = r->next)
        if (r->target == &target && address > r->address &&
            address + size < r->address + r->size) {
            reservation = allocate();
            return reservation != nullptr;
        }
    return true;
}
// Whole removal releases backing; boundary removal trims bounds; a middle hole
// consumes the reserved record and retains backing for its new survivor.
void Maps::completeUnmap(::kernel::task::Task& target, uint64_t address,
                         uint64_t size, void* reserved) {
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    auto* spare        = static_cast<Map*>(reserved);
    const uint64_t end = address + size;
    Map** link         = &mappings;
    while (*link) {
        Map* r = *link;
        if (r->target != &target || !r->overlaps(address, size)) {
            link = &r->next;
            continue;
        }
        const uint64_t oldEnd = r->address + r->size;
        if (address <= r->address && end >= oldEnd) {
            *link = r->next;
            r->object->release();
            pool.release(r);
            continue;
        }
        if (address > r->address && end < oldEnd) {
            // prepareUnmap reserved this node before any PTE was removed.
            *spare         = *r;
            spare->address = end;
            spare->size    = oldEnd - end;
            spare->offset += end - r->address;
            spare->object->retain();
            spare->next = r->next;
            r->next     = spare;
            spare       = nullptr;
            r->size     = address - r->address;
        } else if (address <= r->address) {
            // A prefix removal shifts both virtual start and object offset;
            // changing only one would make future operations refer to the
            // wrong physical bytes.
            r->offset += end - r->address;
            r->address = end;
            r->size    = oldEnd - end;
        } else
            r->size = address - r->address;
        link = &r->next;
    }
    if (spare) pool.release(spare);
}
// Drop metadata/backing references after target execution stops. Root teardown
// is separate and skips external leaves so the same backing is not freed
// twice.
void Maps::releaseTask(::kernel::task::Task& target) {
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    Map** link = &mappings;
    while (*link) {
        Map* r = *link;
        if (r->target != &target) {
            link = &r->next;
            continue;
        }
        *link = r->next;
        r->object->release();
        pool.release(r);
    }
}
// Check all required slots before cloning any record under registry lock. Each
// child mapping retains backing independently of the copied PTE address.
bool Maps::cloneTask(::kernel::task::Task& source,
                     ::kernel::task::Task& target) {
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    uint32_t required = 0;
    for (Map* r = mappings; r; r = r->next)
        if (r->target == &source) ++required;
    if (pool.available() < required) return false;
    Map* copies = nullptr;
    for (Map* r = mappings; r; r = r->next)
        if (r->target == &source) {
            Map* copy    = allocate();
            *copy        = *r;
            copy->target = &target;
            copy->object->retain();
            copy->next = copies;
            copies     = copy;
        }
    while (copies) {
        Map* next    = copies->next;
        copies->next = mappings;
        mappings     = copies;
        copies       = next;
    }
    return true;
}
// Enforce original maximum rights for all overlaps, including COW write
// restrictions. No record means this registry imposes no additional ceiling.
bool Maps::protectionAllowed(::kernel::task::Task& target, uint64_t address,
                             uint64_t size, uint64_t protection) {
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    for (Map* r = mappings; r; r = r->next)
        if (r->target == &target && r->overlaps(address, size) &&
            (((protection & 1) && (!(r->protection & 1) || r->cow)) ||
             ((protection & 2) && !(r->protection & 2))))
            return false;
    return true;
}
// Require the exact registered interval and stable target root. Clear write
// with shootdowns, then mark COW so later generic protection cannot restore
// write without resolving the backing through pager policy.
bool Maps::markCopyOnWrite(::kernel::task::Task& target, uint64_t address,
                           uint64_t size) {
    ::kernel::task::PreemptionSpinLockGuard guard(lock);
    Map* record = nullptr;
    for (Map* r = mappings; r; r = r->next)
        if (r->target == &target && r->address == address && r->size == size) {
            record = r;
            break;
        }
    if (!record) return false;
    const uint64_t root = target.addressSpace().rootPhysicalAddress();
    uint64_t flags      = 0;
    if (!root || !mm::getPhysicalAddressInAddressSpace(root, address, &flags))
        return false;
    for (uint64_t offset = 0; offset < size; offset += 4096)
        if (!mm::protectPageInAddressSpace(root, address + offset,
                                           flags & ~pageWritable))
            return false;
    record->cow = true;
    return true;
}

}  // namespace kernel::mm
