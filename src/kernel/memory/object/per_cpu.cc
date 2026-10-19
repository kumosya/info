#include "kernel/mm.h"
#include "kernel/cpu.h"
namespace kernel::memory {
namespace { bool localReady; }
void PerCpuObjectCache::enableCpuLocal() { __atomic_store_n(&localReady, true, __ATOMIC_RELEASE); }
uint32_t PerCpuObjectCache::currentCpu() {
    return __atomic_load_n(&localReady, __ATOMIC_ACQUIRE) ? cpu::getCurrentId() % cpuCapacity : 0;
}
void PerCpuObjectCache::initialize(size_t objectSize) {
    cache_.initialize(objectSize);
    for (auto& magazine : magazines_) { magazine.lock.init(); magazine.count = 0; }
}
void* PerCpuObjectCache::allocate() {
    {
        const uint64_t flags = interrupt::disable();
        auto& magazine = magazines_[currentCpu()]; magazine.lock.lock();
        void* object = magazine.count ? magazine.objects[--magazine.count] : nullptr;
        magazine.lock.unlock(); interrupt::restore(flags);
        if (object) return object;
    }
    return cache_.allocate();
}
void PerCpuObjectCache::release(void* object) {
    if (!object) return;
    const uint64_t flags = interrupt::disable();
    auto& magazine = magazines_[currentCpu()]; magazine.lock.lock();
    if (magazine.count < magazineCapacity) {
        magazine.objects[magazine.count++] = object;
        magazine.lock.unlock(); interrupt::restore(flags); return;
    }
    magazine.lock.unlock(); interrupt::restore(flags);
    cache_.release(object);
}
uint64_t PerCpuObjectCache::drain() {
    for (auto& magazine : magazines_) {
        InterruptSpinLockGuard guard(magazine.lock);
        while (magazine.count) cache_.release(magazine.objects[--magazine.count]);
    }
    return cache_.shrink();
}
}  // namespace kernel::memory
