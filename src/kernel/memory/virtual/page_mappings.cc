#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "kernel/page.h"
#include "kernel/mm.h"
#include "kernel/task.h"
#include "kernel/new.h"
namespace kernel::memory {
namespace {
struct alignas(16) Mapping {
    Task* target; PageObject* object; uint64_t address, size, offset, protection; Mapping* next; bool cow = false, pagerManaged = false;
};
Mapping storage[2048];
FixedPool pool;
SpinLock lock;
Mapping* mappings;
class Records {
   public:
    static Mapping* allocate() { void* value = pool.allocate(); return value ? new (value) Mapping{} : nullptr; }
    static bool overlaps(const Mapping& record, uint64_t address, uint64_t size) { return record.address < address + size && address < record.address + record.size; }
};
}
bool PageMappings::init() { lock.init(); mappings = nullptr; return pool.init(storage, sizeof(storage), sizeof(Mapping)); }
bool PageMappings::dumpAllowed(Task& target, uint64_t address) {
    PreemptionSpinLockGuard guard(lock);
    for (const Mapping* r = mappings; r; r = r->next)
        if (r->target == &target && r->pagerManaged && address >= r->address && address - r->address < r->size) return true;
    return false;
}
bool PageMappings::map(Task& target, PageObject& object, uint64_t address, uint64_t size, uint64_t offset, uint64_t protection, uint64_t maximumProtection) {
    if (!size || ((address | size | offset) & 4095) || address < 0x10000 || address + size <= address ||
        address + size > 0x0000800000000000ULL || offset > object.size() || size > object.size() - offset || protection > 2 || maximumProtection > 3 || (protection & ~maximumProtection)) return false;
    PreemptionSpinLockGuard space(target.addressSpace().lock());
    if (!target.addressSpace().rootPhysicalAddress()) return false;
    PreemptionSpinLockGuard guard(lock);
    for (Mapping* r = mappings; r; r = r->next) if (r->target == &target && Records::overlaps(*r, address, size)) return false;
    Mapping* record = Records::allocate(); if (!record) return false;
    const uint64_t root = target.addressSpace().rootPhysicalAddress();
    const uint64_t flags = pageUser | pageExternal | ((protection & 1) ? pageWritable : 0) | ((protection & 2) ? 0 : pageNoExecute);
    uint64_t mapped = 0;
    while (mapped < size && memory::mapPageInAddressSpace(root, address + mapped, object.physical() + offset + mapped, flags)) mapped += 4096;
    if (mapped != size) {
        while (mapped) { mapped -= 4096; memory::unmapPageInAddressSpace(root, address + mapped); }
        pool.release(record); return false;
    }
    object.retain(); *record = {&target, &object, address, size, offset, maximumProtection, mappings};
    record->pagerManaged = task::getCurrent() && task::getCurrent() != &target && task::getCurrent()->serviceRole() == os::ServiceRole::Memory;
    mappings = record; return true;
}
bool PageMappings::nextManagedMapping(Task& target, uint64_t cursor, uint64_t& address, uint64_t& size) {
    PreemptionSpinLockGuard guard(lock); const Mapping* selected = nullptr;
    for (const Mapping* r = mappings; r; r = r->next)
        if (r->target == &target && r->pagerManaged && r->address >= cursor && (!selected || r->address < selected->address)) selected = r;
    if (!selected) return false;
    address = selected->address; size = selected->size; return true;
}
bool PageMappings::prepareUnmap(Task& target, uint64_t address, uint64_t size, void*& reservation) {
    reservation = nullptr;
    PreemptionSpinLockGuard guard(lock);
    for (Mapping* r = mappings; r; r = r->next) if (r->target == &target && address > r->address && address + size < r->address + r->size) {
        reservation = Records::allocate(); return reservation != nullptr;
    }
    return true;
}
void PageMappings::completeUnmap(Task& target, uint64_t address, uint64_t size, void* reserved) {
    PreemptionSpinLockGuard guard(lock); auto* spare = static_cast<Mapping*>(reserved);
    const uint64_t end = address + size;
    Mapping** link = &mappings;
    while (*link) {
        Mapping* r = *link;
        if (r->target != &target || !Records::overlaps(*r, address, size)) { link = &r->next; continue; }
        const uint64_t oldEnd = r->address + r->size;
        if (address <= r->address && end >= oldEnd) {
            *link = r->next; r->object->release(); pool.release(r); continue;
        }
        if (address > r->address && end < oldEnd) {
            // prepareUnmap reserved this node before any PTE was removed.
            *spare = *r; spare->address = end; spare->size = oldEnd - end; spare->offset += end - r->address;
            spare->object->retain(); spare->next = r->next; r->next = spare; spare = nullptr;
            r->size = address - r->address;
        } else if (address <= r->address) {
            r->offset += end - r->address; r->address = end; r->size = oldEnd - end;
        } else r->size = address - r->address;
        link = &r->next;
    }
    if (spare) pool.release(spare);
}
void PageMappings::releaseTask(Task& target) {
    PreemptionSpinLockGuard guard(lock); Mapping** link = &mappings;
    while (*link) {
        Mapping* r = *link;
        if (r->target != &target) { link = &r->next; continue; }
        *link = r->next; r->object->release(); pool.release(r);
    }
}
bool PageMappings::cloneTask(Task& source, Task& target) {
    PreemptionSpinLockGuard guard(lock); uint32_t required = 0;
    for (Mapping* r = mappings; r; r = r->next) if (r->target == &source) ++required;
    if (pool.available() < required) return false;
    Mapping* copies = nullptr;
    for (Mapping* r = mappings; r; r = r->next) if (r->target == &source) {
        Mapping* copy = Records::allocate();
        *copy = *r; copy->target = &target; copy->object->retain(); copy->next = copies; copies = copy;
    }
    while (copies) { Mapping* next = copies->next; copies->next = mappings; mappings = copies; copies = next; }
    return true;
}
bool PageMappings::protectionAllowed(Task& target, uint64_t address, uint64_t size, uint64_t protection) {
    PreemptionSpinLockGuard guard(lock);
    for (Mapping* r = mappings; r; r = r->next) if (r->target == &target && Records::overlaps(*r, address, size) &&
        (((protection & 1) && (!(r->protection & 1) || r->cow)) || ((protection & 2) && !(r->protection & 2)))) return false;
    return true;
}
bool PageMappings::markCopyOnWrite(Task& target, uint64_t address, uint64_t size) {
    PreemptionSpinLockGuard guard(lock);
    Mapping* record = nullptr;
    for (Mapping* r = mappings; r; r = r->next) if (r->target == &target && r->address == address && r->size == size) { record = r; break; }
    if (!record) return false;
    const uint64_t root = target.addressSpace().rootPhysicalAddress();
    uint64_t flags = 0;
    if (!root || !memory::getPhysicalAddressInAddressSpace(root, address, &flags)) return false;
    for (uint64_t offset = 0; offset < size; offset += 4096)
        if (!memory::protectPageInAddressSpace(root, address + offset, flags & ~pageWritable)) return false;
    record->cow = true; return true;
}
}  // namespace kernel::memory
