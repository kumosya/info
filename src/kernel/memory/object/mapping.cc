/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/mapping.cc
 * Stable kernel heap aliases and physical-memory initialization.
 */

#include "kernel/mm.h"
#include "kernel/page.h"

namespace kernel::mm {

namespace {
// Stable physical-indexed heap aliases occupy a separate kernel range;
// heapBase + physical identifies a page without consuming a virtual allocator.
constexpr uint64_t heapBase = 0xffff804000000000ULL,
                   heapSize = 64ULL * 1024 * 1024 * 1024;
}  // namespace
// Map a contiguous physical block into its stable heap alias, accepting an
// already identical mapping but rejecting collisions. No physical ownership is
// transferred here; the caller releases its block if alias setup fails.
void* mapKernelPages(uint64_t physical, uint32_t order) {
    if (!physical || order > maximumBuddyOrder || physical >= heapSize ||
        (4096ULL << order) > heapSize - physical)
        return nullptr;
    const uint64_t bytes = 4096ULL << order;
    for (uint64_t offset = 0; offset < bytes; offset += 4096) {
        const uint64_t address  = heapBase + physical + offset;
        const uint64_t existing = mm::getPhysicalAddress(address);
        if (existing == physical + offset) continue;
        // Existing aliases are not removed during rollback. This helper can
        // leave new aliases for an incomplete attempt; block lifetime remains
        // the caller's responsibility and future use still checks each
        // physical translation.
        if (existing || !mm::mapPage(address, physical + offset,
                                     pageWritable | pageNoExecute))
            return nullptr;
    }
    return reinterpret_cast<void*>(heapBase + physical);
}
// Zero a physical byte range through the established direct map before it can
// expose data from a previous owner. Inputs are physical, not user pointers.
void clearPhysicalPages(uint64_t physical, uint64_t bytes) {
    auto* output = reinterpret_cast<uint8_t*>(0xffff800000000000ULL + physical);
    for (uint64_t i = 0; i < bytes; ++i) output[i] = 0;
}
// Invert only the dedicated heap-alias range. This is arithmetic validation,
// not proof that the alias is currently mapped or that the caller owns it.
uint64_t physicalFromKernelPointer(const void* address) {
    const uint64_t value = reinterpret_cast<uint64_t>(address);
    return value >= heapBase && value - heapBase < heapSize ? value - heapBase
                                                            : 0;
}

}  // namespace kernel::mm
