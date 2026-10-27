/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/physical/physical.cc
 * Boot memory reservations and physical allocator initialization.
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"

namespace kernel::mm {

namespace {
constexpr uint64_t directMap = 0xffff800000000000ULL;
PhyAlloc physicalAllocator;
}  // namespace
// Shared physical allocator and boot availability bitmap.
PhyAlloc& phyAlloc() { return physicalAllocator; }
// Boot bitmap bit for one physical page; not the live buddy allocation state.
bool PhyAlloc::test(uint64_t page) const {
    return (available[page / 8] & (1U << (page % 8))) != 0;
}
// Set or clear one boot eligibility bit before constructing buddy free lists.
void PhyAlloc::set(uint64_t page, bool usable) {
    if (usable)
        available[page / 8] |= 1U << (page % 8);
    else
        available[page / 8] &= ~(1U << (page % 8));
}
// Clip byte ranges to the supported physical limit. Usable ranges round inward
// to whole pages; reservations round outward so a partially occupied page is
// never handed to an allocator client.
void PhyAlloc::range(uint64_t first, uint64_t size, bool usable) {
    if (!size || first >= limit) return;

    uint64_t end = first + size;
    if (end < first || end > limit) end = limit;

    const uint64_t start = usable ? (first + 4095) / 4096 : first / 4096;
    const uint64_t last  = usable ? end / 4096 : (end + 4095) / 4096;

    for (uint64_t page = start; page < last; ++page) set(page, usable);
}
// Discover usable RAM, subtract all boot/hardware reservations, reserve a
// contiguous metadata run, then admit same-node runs into BuddyAlloc. This
// bootstrap path cannot use the heap, which depends on this allocator.
void PhyAlloc::init(const BootInfo* boot) {
    for (auto& byte : available) byte = 0;
    highestAddress = 0;
    memoryTopology().init(boot);

    for (uint32_t i = 0; i < boot->getMemoryMapCount(); ++i) {
        const auto& region = boot->getMemoryMap()[i];
        if (region.getType() != memoryAvailable || region.getBase() >= limit)
            continue;
        range(region.getBase(), region.getLength(), true);
        const uint64_t end = region.getBase() + region.getLength();
        const uint64_t bounded =
            end < region.getBase() || end > limit ? limit : end;
        if (bounded > highestAddress) highestAddress = bounded;
    }

    // Firmware holes win over overlapping usable descriptors.
    for (uint32_t i = 0; i < boot->getMemoryMapCount(); ++i)
        if (boot->getMemoryMap()[i].getType() != memoryAvailable)
            range(boot->getMemoryMap()[i].getBase(),
                  boot->getMemoryMap()[i].getLength(), false);

    // Keep low firmware/trampoline memory, bootstrap code/tables, Multiboot
    // data, modules and framebuffer outside free RAM even if firmware labels
    // them usable.
    range(0, 1024 * 1024, false);
    range(boot->getBootstrapStart(),
          boot->getBootstrapEnd() - boot->getBootstrapStart(), false);
    range(boot->getMultibootAddress(), boot->getMultibootSize(), false);
    for (uint32_t i = 0; i < boot->getModuleCount(); ++i)
        range(boot->getModules()[i].getStart(),
              boot->getModules()[i].getEnd() - boot->getModules()[i].getStart(),
              false);

    range(boot->getFramebuffer().getAddress(),
          static_cast<uint64_t>(boot->getFramebuffer().getPitch()) *
              boot->getFramebuffer().getHeight(),
          false);
    const uint32_t count = highestAddress / 4096;
    // One PageFrame per managed physical frame needs RAM of its own. Find the
    // metadata run using the boot bitmap, then reserve it before populating
    // free lists, or the allocator could later overwrite its own bookkeeping.
    const uint64_t metadataPages =
        (static_cast<uint64_t>(count) * sizeof(PageFrame) + 4095) / 4096;
    uint64_t run = 0, metadata = 0;
    for (uint32_t page = 256; page < count; ++page) {
        run = test(page) ? run + 1 : 0;
        if (run == metadataPages) {
            metadata = page + 1 - run;
            break;
        }
    }
    if (!metadata || !metadataPages) {
        device::serialWrite("[MEMORY] insufficient page metadata storage\n");
        cpu::halt();
    }
    range(metadata * 4096, metadataPages * 4096, false);
    allocator.init(reinterpret_cast<PageFrame*>(directMap + metadata * 4096),
                   count);
    // Group only consecutive eligible pages on the same NUMA node. Holes and
    // node changes end a run; buddy merging must never bridge either boundary.
    for (uint32_t page = 256; page < count;) {
        if (!test(page)) {
            ++page;
            continue;
        }
        const uint32_t first = page;
        const uint16_t node =
            memoryTopology().nodeForAddress(static_cast<uint64_t>(page) * 4096);
        while (page < count && test(page) &&
               memoryTopology().nodeForAddress(static_cast<uint64_t>(page) *
                                               4096) == node)
            ++page;
        allocator.addRange(first, page - first, node);
    }
    device::serialWrite("[MEMORY] NUMA buddy initialized, free pages=");
    device::serialWriteDecimal(allocator.freePages());
    device::serialWrite(" nodes=");
    device::serialWriteDecimal(memoryTopology().nodeCount());
    device::serialWrite("\n");
}

// An unspecified node prefers the current CPU proximity node, with fallback
// controlled by strict. maximum bounds the physical end of the returned block.
uint64_t PhyAlloc::allocate(uint32_t order, uint64_t owner, uint16_t node,
                            bool strict, uint64_t maximum) {
    return allocator.allocate(
        order, owner,
        node == anyMemoryNode ? memoryTopology().currentNode() : node, strict,
        maximum);
}

// Boot entry for physical memory setup, before heap or user mappings exist.
void initPhy(const BootInfo* boot) { phyAlloc().init(boot); }

// Allocate 2^order contiguous pages with owner/node/limit constraints. Return
// a physical address, not a dereferenceable kernel virtual pointer.
uint64_t allocatePhysicalBlock(uint32_t order, uint64_t owner, uint16_t node,
                               bool strict, uint64_t maximum) {
    return phyAlloc().allocate(order, owner, node, strict, maximum);
}
// Order-zero physical allocation through the same block ownership rules.
uint64_t allocatePhysicalPage() { return allocatePhysicalBlock(0); }
// Trusted order-zero release, bypassing owner comparison but not block state
// and order validation. Shared pages require balanced retain/release instead.
void freePhysicalPage(uint64_t address) {
    phyAlloc().release(address, 0, UINT64_MAX);
}
// Add one block reference after validating the requested owner.
bool retainPhysicalBlock(uint64_t address, uint64_t owner) {
    return phyAlloc().retain(address, owner);
}
// Drop one matching block reference; zero references returns it to buddy
// lists.
bool releasePhysicalBlock(uint64_t address, uint32_t order, uint64_t owner) {
    return phyAlloc().release(address, order, owner);
}
// Assign or confirm physical ownership without changing reference count.
bool assignPhysicalPageOwner(uint64_t address, uint64_t owner) {
    return phyAlloc().assignOwner(address, owner);
}
// Block metadata for the physical address, including owner, node and order.
bool queryPhysicalPage(uint64_t address, PhysicalPageInfo& info) {
    return phyAlloc().query(address, info);
}
// Total allocated bytes attributed to an owner, counting each block once.
uint64_t getOwnedMemoryBytes(uint64_t owner) {
    return phyAlloc().ownedBytes(owner);
}
// Managed physical-memory span reported by PhyAlloc.
uint64_t getTotalMemoryBytes() { return phyAlloc().totalBytes(); }
// Currently free buddy pages converted to bytes.
uint64_t getFreeMemoryBytes() { return phyAlloc().freeBytes(); }

}  // namespace kernel::mm
