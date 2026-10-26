#include "kernel/mm.h"
#include "kernel/page.h"
namespace kernel::memory {
namespace { constexpr uint64_t slabMagic = 0x534c414243414348ULL; }
struct alignas(16) ObjectCache::Slab {
    uint64_t magic;
    ObjectCache* owner;
    Slab* previous;
    Slab* next;
    void* first;
    uint64_t used[4];
    uint32_t capacity, live;
};
void ObjectCache::init(size_t size) {
    lock_.init(); slabs_ = nullptr; empty_ = 0;
    stride_ = size && size <= 2048 ? (size + 15) & ~15ULL : 0;
}
void ObjectCache::unlink(Slab* slab) {
    if (slab->previous) slab->previous->next = slab->next; else slabs_ = slab->next;
    if (slab->next) slab->next->previous = slab->previous;
}
void* ObjectCache::allocate() {
    if (!stride_) return nullptr;
    InterruptSpinLockGuard guard(lock_);
    Slab* slab = slabs_;
    while (slab && !slab->first) slab = slab->next;
    if (!slab) {
        const uint64_t physical = allocatePhysicalBlock(0);
        if (!physical) return nullptr;
        slab = static_cast<Slab*>(KernelMapping::map(physical, 0));
        if (!slab) { releasePhysicalBlock(physical, 0); return nullptr; }
        slab->magic = slabMagic; slab->owner = this; slab->previous = nullptr; slab->next = slabs_;
        if (slabs_) slabs_->previous = slab;
        slabs_ = slab;
        slab->capacity = (4096 - sizeof(Slab)) / stride_; slab->live = 0;
        for (auto& word : slab->used) word = 0;
        slab->first = nullptr;
        for (uint32_t i = 0; i < slab->capacity; ++i) {
            auto* object = reinterpret_cast<uint8_t*>(slab + 1) + i * stride_;
            *reinterpret_cast<void**>(object) = slab->first; slab->first = object;
        }
        ++empty_;
    }
    void* object = slab->first;
    slab->first = *static_cast<void**>(object);
    const uint32_t index = (static_cast<uint8_t*>(object) - reinterpret_cast<uint8_t*>(slab + 1)) / stride_;
    slab->used[index / 64] |= 1ULL << (index % 64);
    if (!slab->live++) --empty_;
    return object;
}
bool ObjectCache::release(void* object) {
    if (!object || !stride_) return false;
    auto* slab = reinterpret_cast<Slab*>(reinterpret_cast<uint64_t>(object) & ~4095ULL);
    InterruptSpinLockGuard guard(lock_);
    if (slab->magic != slabMagic || slab->owner != this || !slab->live) return false;
    const uint64_t offset = static_cast<uint8_t*>(object) - reinterpret_cast<uint8_t*>(slab + 1);
    if (offset % stride_ || offset / stride_ >= slab->capacity) return false;
    const uint32_t index = offset / stride_; const uint64_t bit = 1ULL << (index % 64);
    if (!(slab->used[index / 64] & bit)) return false;
    slab->used[index / 64] &= ~bit;
    *static_cast<void**>(object) = slab->first; slab->first = object;
    if (--slab->live == 0) {
        if (empty_) { unlink(slab); slab->magic = 0; releasePhysicalBlock(KernelMapping::physical(slab), 0); }
        else ++empty_;
    }
    return true;
}
uint64_t ObjectCache::shrink() {
    InterruptSpinLockGuard guard(lock_); uint64_t pages = 0;
    for (Slab* slab = slabs_; slab;) {
        Slab* next = slab->next;
        if (!slab->live) { unlink(slab); slab->magic = 0; --empty_; releasePhysicalBlock(KernelMapping::physical(slab), 0); ++pages; }
        slab = next;
    }
    return pages;
}
}  // namespace kernel::memory
