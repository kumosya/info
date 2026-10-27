/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/heap.cc
 * Kernel heap size classes, allocation headers, and cache reclamation.
 */

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"

namespace kernel::mm {

namespace {
constexpr uint64_t allocatedMagic = 0x48454150414c4c4fULL;
constexpr uint64_t freedMagic     = 0x4845415046524545ULL;
// Size classes include the private Header, not just caller payload bytes.
constexpr uint32_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048};
mm::PerCpuObjectCache caches[7];
// Prefix every allocation with ownership-routing metadata while preserving
// 16-byte payload alignment. bucket selects a slab cache; UINT16_MAX selects a
// whole buddy block whose order is recorded separately.
struct alignas(16) Header {
    uint64_t magic;
    uint32_t order;
    uint16_t bucket;
    uint16_t reserved;
};
}  // namespace
// Initialize caches only after physical allocation and kernel mapping work.
// CPU magazines remain in bootstrap mode until CPU-local data is published.
void initHeap() {
    for (uint32_t i = 0; i < 7; ++i) caches[i].init(sizes[i]);
    device::serialWrite(
        "[MEMORY] slab, per-CPU magazines and large-block buddy heap "
        "initialized\n");
}
// Reject invalid sizes, include the header, then choose the smallest fitting
// cache. Larger objects use one contiguous power-of-two physical block plus a
// stable kernel alias. Mapping failure returns the newly owned block.
void* allocateHeap(size_t size) {
    if (!size || size > (1ULL << 30) - sizeof(Header)) return nullptr;
    const uint64_t required = size + sizeof(Header);
    for (uint16_t i = 0; i < 7; ++i)
        if (required <= sizes[i]) {
            auto* header = static_cast<Header*>(caches[i].allocate());
            if (!header) return nullptr;
            header->magic  = allocatedMagic;
            header->bucket = i;
            header->order  = 0;
            return header + 1;
        }
    // Round the full header-plus-payload size to physical pages; the stored
    // order must later match the allocated head before whole-block release is
    // permitted.
    uint32_t order = 0;
    while ((4096ULL << order) < required) ++order;
    const auto physical = mm::allocatePhysicalBlock(order);
    if (!physical) return nullptr;
    auto* header = static_cast<Header*>(mm::mapKernelPages(physical, order));
    if (!header) {
        mm::releasePhysicalBlock(physical, order);
        return nullptr;
    }
    header->magic  = allocatedMagic;
    header->bucket = UINT16_MAX;
    header->order  = order;
    return header + 1;
}
// Validate kernel-alias and physical ownership metadata before routing a
// payload back to its allocator. Claim the allocated magic atomically so an
// already freed header is not inserted into a free list twice. This is not
// permission to pass arbitrary or already-unmapped addresses.
void freeHeap(void* object) {
    if (!object || (reinterpret_cast<uint64_t>(object) & 15)) return;
    auto* header            = static_cast<Header*>(object) - 1;
    const auto address      = reinterpret_cast<uint64_t>(header);
    const uint64_t physical = mm::physicalFromKernelPointer(header);
    if (!physical) return;
    mm::PhysicalPageInfo info{};
    if (!mm::queryPhysicalPage(physical, info) ||
        info.getState() != mm::FrameState::Allocated || info.getOwner())
        return;
    uint64_t expected = allocatedMagic;
    // Only one releaser may consume this allocation header. The winner can
    // hand the bytes to a magazine or overwrite them with slab free-list
    // links.
    if (!__atomic_compare_exchange_n(&header->magic, &expected, freedMagic,
                                     false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return;
    if (header->bucket < 7)
        caches[header->bucket].release(header);
    else if (!(address & 4095) && header->bucket == UINT16_MAX &&
             header->order == info.getOrder())
        mm::releasePhysicalBlock(physical, header->order);
}
// Drain CPU magazines before shrinking empty slabs; cached objects still count
// as slab-live until returned, so shrinking first would miss those pages.
// Return reclaimed bytes, unlike the page-count cache helpers.
uint64_t reclaimCaches() {
    uint64_t pages = 0;
    for (auto& cache : caches) pages += cache.drain();
    return pages * 4096;
}

}  // namespace kernel::mm
