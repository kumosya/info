#include "service/mm/page.h"
namespace mm {
bool RegionTable::validRange(uint64_t address, uint64_t size) {
    return size && !((address | size) & 4095) && address >= lower &&
           address < upper && size <= upper - address;
}
const Region* RegionTable::find(uint64_t task, uint64_t address) const {
    for (const auto& r : regions_)
        if (r.used && r.task == task && address >= r.address &&
            address - r.address < r.size)
            return &r;
    return nullptr;
}
Region* RegionTable::find(uint64_t task, uint64_t address) {
    return const_cast<Region*>(
        static_cast<const RegionTable*>(this)->find(task, address));
}
Region* RegionTable::vacant() {
    for (auto& r : regions_)
        if (!r.used) return &r;
    return nullptr;
}
size_t RegionTable::vacancies() const {
    size_t count = 0;
    for (const auto& r : regions_)
        if (!r.used) ++count;
    return count;
}
uint64_t RegionTable::findGap(uint64_t task, uint64_t size,
                              uint64_t hint) const {
    if (!size || (size & 4095) || hint > upper - 4096) return 0;
    uint64_t candidate = (hint + 4095) & ~4095ULL;
    if (candidate < lower) candidate = lower;
    while (validRange(candidate, size)) {
        uint64_t next = candidate;
        for (const auto& r : regions_)
            if (r.used && r.task == task && r.address < candidate + size &&
                candidate < r.address + r.size && next < r.address + r.size)
                next = r.address + r.size;
        if (next == candidate) return candidate;
        candidate = next;
    }
    return 0;
}
bool RegionTable::insert(const Region& region) {
    if (!region.task || !validRange(region.address, region.size) ||
        (region.offset & 4095) || region.offset > UINT64_MAX - region.size ||
        (region.protection & ~7U) || (region.maximumProtection & ~7U) ||
        (region.protection & ~region.maximumProtection) ||
        (region.protection & 6U) == 6U)
        return false;
    for (const auto& r : regions_)
        if (r.used && r.task == region.task &&
            r.address < region.address + region.size &&
            region.address < r.address + r.size)
            return false;
    auto* slot = vacant();
    if (!slot) return false;
    *slot      = region;
    slot->used = true;
    merge(region.task);
    return true;
}
bool RegionTable::covered(uint64_t task, uint64_t address,
                          uint64_t size) const {
    if (!validRange(address, size)) return false;
    const uint64_t end = address + size;
    while (address < end) {
        const auto* r = find(task, address);
        if (!r) return false;
        address = r->address + r->size;
    }
    return true;
}
bool RegionTable::remove(uint64_t task, uint64_t address, uint64_t size) {
    if (!validRange(address, size)) return false;
    const uint64_t end = address + size;
    for (const auto& r : regions_)
        if (r.used && r.task == task && address > r.address &&
            end < r.address + r.size && !vacancies())
            return false;
    for (auto& r : regions_) {
        if (!r.used || r.task != task || r.address >= end ||
            address >= r.address + r.size)
            continue;
        const uint64_t oldEnd = r.address + r.size;
        if (address <= r.address && end >= oldEnd)
            r = {};
        else if (address > r.address && end < oldEnd) {
            Region* right  = vacant();
            *right         = r;
            right->address = end;
            right->size    = oldEnd - end;
            right->offset += end - r.address;
            r.size = address - r.address;
        } else if (address <= r.address) {
            r.offset += end - r.address;
            r.address = end;
            r.size    = oldEnd - end;
        } else
            r.size = address - r.address;
    }
    merge(task);
    return true;
}
bool RegionTable::protect(uint64_t task, uint64_t address, uint64_t size,
                          uint32_t protection) {
    if ((protection & ~7U) || (protection & 6U) == 6U ||
        !covered(task, address, size))
        return false;
    const uint64_t end = address + size;
    size_t required    = 0;
    for (const auto& r : regions_) {
        if (!r.used || r.task != task || r.address >= end ||
            address >= r.address + r.size)
            continue;
        if (protection & ~r.maximumProtection) return false;
        if (address > r.address) ++required;
        if (end < r.address + r.size) ++required;
    }
    if (vacancies() < required) return false;
    for (size_t i = 0; i < capacity; ++i) {
        Region* r = &regions_[i];
        if (!r->used || r->task != task || r->address >= end ||
            address >= r->address + r->size)
            continue;
        if (address > r->address) {
            Region* middle  = vacant();
            *middle         = *r;
            middle->address = address;
            middle->size -= address - r->address;
            middle->offset += address - r->address;
            r->size = address - r->address;
            r       = middle;
        }
        if (end < r->address + r->size) {
            Region* right  = vacant();
            *right         = *r;
            right->address = end;
            right->size -= end - r->address;
            right->offset += end - r->address;
            r->size = end - r->address;
        }
        r->protection = protection;
    }
    merge(task);
    return true;
}
void RegionTable::merge(uint64_t task) {
    bool changed;
    do {
        changed = false;
        for (auto& a : regions_)
            if (a.used && a.task == task)
                for (auto& b : regions_) {
                    if (&a == &b || !b.used || b.task != task ||
                        a.address + a.size != b.address ||
                        a.offset + a.size != b.offset ||
                        a.backing != b.backing ||
                        a.protection != b.protection ||
                        a.maximumProtection != b.maximumProtection ||
                        a.kind != b.kind || a.lazy != b.lazy ||
                        a.shared != b.shared)
                        continue;
                    a.size += b.size;
                    b       = {};
                    changed = true;
                }
    } while (changed);
}
void RegionTable::removeTask(uint64_t task) {
    for (auto& r : regions_)
        if (r.used && r.task == task) r = {};
}
size_t RegionTable::references(uint64_t backing) const {
    size_t count = 0;
    for (const auto& r : regions_)
        if (r.used && r.backing == backing) ++count;
    return count;
}
}  // namespace mm
