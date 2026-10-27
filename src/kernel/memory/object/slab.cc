/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/slab.cc
 * Page-backed object slabs, allocation validation, and empty-page release.
 */

#include "kernel/mm.h"
#include "kernel/page.h"

namespace kernel::mm {

namespace {
constexpr uint64_t slabMagic = 0x534c414243414348ULL;
}
// One physical page contains this header plus equal-stride object slots. Free
// slots store their next pointer in their own bytes; used bits validate
// releases, while live/empty counts drive page retention and reclamation.
class alignas(16) ObjectCache::Slab {
    friend class ObjectCache;

   public:
    // Whether the intrusive free-slot list is nonempty.
    bool hasSpace() const { return first != nullptr; }

   private:
    uint64_t magic;
    ObjectCache* owner;
    Slab* previous;
    Slab* next;
    void* first;
    uint64_t used[4];
    uint32_t capacity, live;
};
// Align one supported object size to sixteen bytes; zero stride disables
// allocation. Initialize before any slab or magazine can reference this cache.
void ObjectCache::init(size_t size) {
    lock_.init();
    slabs_  = nullptr;
    empty_  = 0;
    stride_ = size && size <= 2048 ? (size + 15) & ~15ULL : 0;
}
// Caller holds lock_; detach only list membership, leaving physical page
// release to the path that has proved no live slot remains.
void ObjectCache::unlink(Slab* slab) {
    if (slab->previous)
        slab->previous->next = slab->next;
    else
        slabs_ = slab->next;
    if (slab->next) slab->next->previous = slab->previous;
}
// Find a slab with space under the cache lock or allocate/map one page. Link
// and initialize a new slab before consuming a slot. If mapping fails, return
// its physical page without publishing a partially built slab.
void* ObjectCache::allocate() {
    if (!stride_) return nullptr;
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    Slab* slab = slabs_;
    while (slab && !slab->hasSpace()) slab = slab->next;
    if (!slab) {
        const uint64_t physical = allocatePhysicalBlock(0);
        if (!physical) return nullptr;
        slab = static_cast<Slab*>(mapKernelPages(physical, 0));
        if (!slab) {
            releasePhysicalBlock(physical, 0);
            return nullptr;
        }
        slab->magic    = slabMagic;
        slab->owner    = this;
        slab->previous = nullptr;
        slab->next     = slabs_;
        if (slabs_) slabs_->previous = slab;
        slabs_         = slab;
        slab->capacity = (4096 - sizeof(Slab)) / stride_;
        slab->live     = 0;
        for (auto& word : slab->used) word = 0;
        slab->first = nullptr;
        // Only free slots carry next pointers; allocated payload must never be
        // used as a link. The bitmap separately records which slot addresses
        // are live.
        for (uint32_t i = 0; i < slab->capacity; ++i) {
            auto* object = reinterpret_cast<uint8_t*>(slab + 1) + i * stride_;
            *reinterpret_cast<void**>(object) = slab->first;
            slab->first                       = object;
        }
        ++empty_;
    }
    void* object = slab->first;
    slab->first  = *static_cast<void**>(object);
    const uint32_t index =
        (static_cast<uint8_t*>(object) - reinterpret_cast<uint8_t*>(slab + 1)) /
        stride_;
    slab->used[index / 64] |= 1ULL << (index % 64);
    if (!slab->live++) --empty_;
    return object;
}
// Recover the page-aligned slab header, then validate cache ownership, exact
// slot alignment and its used bit under lock. Reject an interior pointer or
// duplicate release instead of corrupting the intrusive free list.
bool ObjectCache::release(void* object) {
    if (!object || !stride_) return false;
    auto* slab =
        reinterpret_cast<Slab*>(reinterpret_cast<uint64_t>(object) & ~4095ULL);
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    if (slab->magic != slabMagic || slab->owner != this || !slab->live)
        return false;
    const uint64_t offset =
        static_cast<uint8_t*>(object) - reinterpret_cast<uint8_t*>(slab + 1);
    if (offset % stride_ || offset / stride_ >= slab->capacity) return false;
    const uint32_t index = offset / stride_;
    const uint64_t bit   = 1ULL << (index % 64);
    if (!(slab->used[index / 64] & bit)) return false;
    slab->used[index / 64] &= ~bit;
    *static_cast<void**>(object) = slab->first;
    slab->first                  = object;
    // Retain one empty slab for reuse, but return additional empty slabs to
    // the physical allocator. Mark the detached slab invalid before releasing
    // RAM.
    if (--slab->live == 0) {
        if (empty_) {
            unlink(slab);
            slab->magic = 0;
            releasePhysicalBlock(physicalFromKernelPointer(slab), 0);
        } else
            ++empty_;
    }
    return true;
}
// Release all currently empty slabs, saving next before freeing each page. The
// cache lock protects traversal and live counts; return reclaimed pages.
uint64_t ObjectCache::shrink() {
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    uint64_t pages = 0;
    for (Slab* slab = slabs_; slab;) {
        Slab* next = slab->next;
        if (!slab->live) {
            unlink(slab);
            slab->magic = 0;
            --empty_;
            releasePhysicalBlock(physicalFromKernelPointer(slab), 0);
            ++pages;
        }
        slab = next;
    }
    return pages;
}

}  // namespace kernel::mm
