#include "kernel/mm.h"
#include "kernel/page.h"
namespace kernel::memory {
namespace { constexpr uint64_t heapBase = 0xffff804000000000ULL, heapSize = 64ULL * 1024 * 1024 * 1024; }
void* KernelMapping::map(uint64_t physical, uint32_t order) {
    if (!physical || order > maximumBuddyOrder || physical >= heapSize || (4096ULL << order) > heapSize - physical) return nullptr;
    const uint64_t bytes = 4096ULL << order;
    for (uint64_t offset = 0; offset < bytes; offset += 4096) {
        const uint64_t address = heapBase + physical + offset;
        const uint64_t existing = memory::getPhysicalAddress(address);
        if (existing == physical + offset) continue;
        if (existing || !memory::mapPage(address, physical + offset, pageWritable | pageNoExecute)) return nullptr;
    }
    return reinterpret_cast<void*>(heapBase + physical);
}
void KernelMapping::clear(uint64_t physical, uint64_t bytes) {
    auto* output = reinterpret_cast<uint8_t*>(0xffff800000000000ULL + physical);
    for (uint64_t i = 0; i < bytes; ++i) output[i] = 0;
}
uint64_t KernelMapping::physical(const void* address) {
    const uint64_t value = reinterpret_cast<uint64_t>(address);
    return value >= heapBase && value - heapBase < heapSize ? value - heapBase : 0;
}
}  // namespace kernel::memory
