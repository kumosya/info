/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/physical/topology.cc
 * SRAT proximity domains, physical ranges, and CPU-local NUMA selection.
 */

#include "kernel/acpi.h"
#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"

namespace kernel::mm {

namespace {

MemoryTopology topology;

// Read little-endian firmware fields bytewise; SRAT entries need not satisfy
// C++ integer alignment, so do not cast their byte offsets to native pointers.
uint64_t readSratValue(const uint8_t* data, unsigned bytes) {
    uint64_t result = 0;
    for (unsigned i = 0; i < bytes; ++i)
        result |= static_cast<uint64_t>(data[i]) << (i * 8);
    return result;
}

}  // namespace

// Boot-discovered proximity topology shared by physical allocation clients.
MemoryTopology& memoryTopology() { return topology; }

// Map sparse firmware proximity-domain IDs to bounded dense allocator nodes.
// Return anyMemoryNode when no new node slot remains instead of overrunning
// per-node free-list arrays.
uint16_t MemoryTopology::nodeForDomain(uint32_t domain) {
    for (uint16_t i = 0; i < nodes; ++i)
        if (domains[i] == domain) return i;
    if (nodes == maximumMemoryNodes) return anyMemoryNode;
    domains[nodes] = domain;
    return nodes++;
}

// Start with node zero as fallback, then parse enabled SRAT memory and
// processor affinities. Validate entry lengths before reading type-specific
// fields; absent or malformed tables leave the bounded fallback usable.
void MemoryTopology::init(const BootInfo* boot) {
    nodes      = 1;
    domains[0] = 0;
    rangeCount = cpuCount = 0;
    cpuLocalReady         = false;
    const auto* table     = acpi::Tables(boot).find("SRAT");
    if (!table || table->length < sizeof(acpi::AcpiSdtHeader) + 12) return;
    const auto* data = reinterpret_cast<const uint8_t*>(table);
    for (uint32_t offset = sizeof(acpi::AcpiSdtHeader) + 12;
         offset + 2 <= table->length;) {
        const auto* entry     = data + offset;
        const uint32_t length = entry[1];
        if (length < 2 || length > table->length - offset) break;
        // Memory-affinity entries describe physical half-open ranges. Reject
        // empty or wrapping extents and cap recorded ranges independently of
        // node count.
        if (entry[0] == 1 && length >= 40 &&
            (readSratValue(entry + 28, 4) & 1)) {
            const uint16_t node  = nodeForDomain(readSratValue(entry + 2, 4));
            const uint64_t first = readSratValue(entry + 8, 8),
                           size  = readSratValue(entry + 16, 8);
            if (node != anyMemoryNode && rangeCount < 64 && size &&
                first + size > first)
                ranges[rangeCount++] = {first, first + size, node};
        } else if (entry[0] == 0 && length >= 16 &&
                   (readSratValue(entry + 4, 4) & 1)) {
            // Legacy APIC entries split the domain number across nonadjacent
            // bytes; x2APIC entries use a full-width ID and their own fixed
            // offsets.
            const uint32_t domain =
                entry[2] | (readSratValue(entry + 9, 3) << 8);
            const uint16_t node = nodeForDomain(domain);
            if (node != anyMemoryNode && cpuCount < 256)
                cpus[cpuCount++] = {entry[3], node};
        } else if (entry[0] == 2 && length >= 24 &&
                   (readSratValue(entry + 12, 4) & 1)) {
            const uint16_t node = nodeForDomain(readSratValue(entry + 4, 4));
            if (node != anyMemoryNode && cpuCount < 256)
                cpus[cpuCount++] = {
                    static_cast<uint32_t>(readSratValue(entry + 8, 4)), node};
        }
        offset += length;
    }
}

// Choose the node containing a physical address, defaulting to node zero when
// firmware did not describe that address.
uint16_t MemoryTopology::nodeForAddress(uint64_t address) {
    for (uint32_t i = 0; i < rangeCount; ++i)
        if (ranges[i].contains(address)) return ranges[i].node;
    return 0;
}

// Publish only after GS/per-CPU state exists. Earlier physical allocations
// must not read current CPU data during the bootstrap dependency cycle.
void MemoryTopology::enableCpuLocal() {
    __atomic_store_n(&cpuLocalReady, true, __ATOMIC_RELEASE);
}

// Translate current APIC identity to its memory node after CPU-local setup.
// Before publication or without an affinity record, safely prefer node zero.
uint16_t MemoryTopology::currentNode() {
    if (!__atomic_load_n(&cpuLocalReady, __ATOMIC_ACQUIRE)) return 0;
    const uint32_t apic = cpu::getCurrent()->getApicId();
    for (uint32_t i = 0; i < cpuCount; ++i)
        if (cpus[i].matches(apic)) return cpus[i].node;
    return 0;
}

// Number of dense memory-node slots discovered at boot.
uint16_t MemoryTopology::nodeCount() { return nodes; }

}  // namespace kernel::mm
