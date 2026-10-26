#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"

namespace kernel::heap {
namespace {
constexpr uint64_t allocatedMagic = 0x48454150414c4c4fULL;
constexpr uint64_t freedMagic = 0x4845415046524545ULL;
constexpr uint32_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048};
memory::PerCpuObjectCache caches[7];
struct alignas(16) Header { uint64_t magic; uint32_t order; uint16_t bucket; uint16_t reserved; };
}
void init() {
    for (uint32_t i = 0; i < 7; ++i) caches[i].init(sizes[i]);
    serial::serialWrite("[MEMORY] slab, per-CPU magazines and large-block buddy heap initialized\n");
}
void* kernelAllocate(size_t size) {
    if (!size || size > (1ULL << 30) - sizeof(Header)) return nullptr;
    const uint64_t required = size + sizeof(Header);
    for (uint16_t i = 0; i < 7; ++i) if (required <= sizes[i]) {
        auto* header = static_cast<Header*>(caches[i].allocate());
        if (!header) return nullptr;
        header->magic = allocatedMagic; header->bucket = i; header->order = 0;
        return header + 1;
    }
    uint32_t order = 0;
    while ((4096ULL << order) < required) ++order;
    const auto physical = memory::allocatePhysicalBlock(order);
    if (!physical) return nullptr;
    auto* header = static_cast<Header*>(memory::KernelMapping::map(physical, order));
    if (!header) { memory::releasePhysicalBlock(physical, order); return nullptr; }
    header->magic = allocatedMagic; header->bucket = UINT16_MAX; header->order = order;
    return header + 1;
}
void kernelFree(void* object) {
    if (!object || (reinterpret_cast<uint64_t>(object) & 15)) return;
    auto* header = static_cast<Header*>(object) - 1;
    const auto address = reinterpret_cast<uint64_t>(header);
    const uint64_t physical = memory::KernelMapping::physical(header);
    if (!physical) return;
    memory::PhysicalPageInfo info{};
    if (!memory::queryPhysicalPage(physical, info) || info.state != memory::FrameState::Allocated || info.owner) return;
    uint64_t expected = allocatedMagic;
    if (!__atomic_compare_exchange_n(&header->magic, &expected, freedMagic, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    if (header->bucket < 7) caches[header->bucket].release(header);
    else if (!(address & 4095) && header->bucket == UINT16_MAX && header->order == info.order)
        memory::releasePhysicalBlock(physical, header->order);
}
uint64_t reclaimCaches() {
    uint64_t pages = 0;
    for (auto& cache : caches) pages += cache.drain();
    return pages * 4096;
}
}  // namespace kernel::heap
