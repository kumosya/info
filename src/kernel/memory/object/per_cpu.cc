/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/per_cpu.cc
 * Per-CPU object magazines backed by a shared slab cache.
 */

#include "kernel/cpu.h"
#include "kernel/mm.h"

namespace kernel::mm {

namespace {
bool localReady;
}
// Publish CPU-local indexing only after current CPU state is initialized.
void enableCpuObjectCaches() {
    __atomic_store_n(&localReady, true, __ATOMIC_RELEASE);
}
// Magazine index for this CPU; bootstrap uses zero before GS data exists.
uint32_t PerCpuObjectCache::currentCpu() {
    return __atomic_load_n(&localReady, __ATOMIC_ACQUIRE)
               ? cpu::getCurrentId() % cpuCapacity
               : 0;
}
// Initialize the common slab cache and each bounded magazine before use.
void PerCpuObjectCache::init(size_t objectSize) {
    cache_.init(objectSize);
    for (auto& magazine : magazines_) {
        magazine.lock.init();
        magazine.count = 0;
    }
}
// Try the local magazine with interrupts excluded, then fall back to the
// shared cache after releasing its lock. A cached slot remains allocated in
// the slab until drained, even while no heap client owns its payload.
void* PerCpuObjectCache::allocate() {
    {
        const uint64_t flags = cpu::disableInterrupts();
        auto& magazine       = magazines_[currentCpu()];
        magazine.lock.lock();
        void* object = magazine.take();
        magazine.lock.unlock();
        cpu::restoreInterrupts(flags);
        if (object) return object;
    }
    return cache_.allocate();
}
// Cache locally if capacity permits; otherwise return to the common slab
// outside the magazine lock. A remote free may enter the freeing CPU's
// magazine rather than the original allocating CPU's magazine.
void PerCpuObjectCache::release(void* object) {
    if (!object) return;
    const uint64_t flags = cpu::disableInterrupts();
    auto& magazine       = magazines_[currentCpu()];
    magazine.lock.lock();
    if (magazine.count < magazineCapacity) {
        magazine.objects[magazine.count++] = object;
        magazine.lock.unlock();
        cpu::restoreInterrupts(flags);
        return;
    }
    magazine.lock.unlock();
    cpu::restoreInterrupts(flags);
    cache_.release(object);
}
// Take each magazine lock before returning its slots, then shrink the common
// cache. No allocation path holds the slab lock while acquiring a magazine, so
// this magazine-to-cache drain order does not invert that path.
uint64_t PerCpuObjectCache::drain() {
    for (auto& magazine : magazines_) {
        ::kernel::task::InterruptSpinLockGuard guard(magazine.lock);
        while (magazine.count)
            cache_.release(magazine.objects[--magazine.count]);
    }
    return cache_.shrink();
}

}  // namespace kernel::mm
