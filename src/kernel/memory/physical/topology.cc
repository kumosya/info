#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/acpi.h"
#include "kernel/cpu.h"

namespace kernel::memory {
namespace {
struct Range { uint64_t first, end; uint16_t node; } ranges[64];
struct Cpu { uint32_t apic; uint16_t node; } cpus[256];
uint32_t domains[maximumMemoryNodes];
uint16_t nodes = 1;
uint32_t rangeCount, cpuCount;
bool cpuLocalReady;
class SratReader {
   public:
    static uint64_t value(const uint8_t* data, unsigned bytes) {
        uint64_t result = 0; for (unsigned i = 0; i < bytes; ++i) result |= static_cast<uint64_t>(data[i]) << (i * 8); return result;
    }
    static uint16_t node(uint32_t domain) {
        for (uint16_t i = 0; i < nodes; ++i) if (domains[i] == domain) return i;
        if (nodes == maximumMemoryNodes) return anyMemoryNode;
        domains[nodes] = domain; return nodes++;
    }
};
}
void MemoryTopology::init(const BootInfo* boot) {
    nodes = 1; domains[0] = 0; rangeCount = cpuCount = 0; cpuLocalReady = false;
    const auto* table = acpi::findAcpiTable(boot, "SRAT");
    if (!table || table->length < sizeof(AcpiSdtHeader) + 12) return;
    const auto* data = reinterpret_cast<const uint8_t*>(table);
    for (uint32_t offset = sizeof(AcpiSdtHeader) + 12; offset + 2 <= table->length;) {
        const auto* entry = data + offset; const uint32_t length = entry[1];
        if (length < 2 || length > table->length - offset) break;
        if (entry[0] == 1 && length >= 40 && (SratReader::value(entry + 28, 4) & 1)) {
            const uint16_t node = SratReader::node(SratReader::value(entry + 2, 4));
            const uint64_t first = SratReader::value(entry + 8, 8), size = SratReader::value(entry + 16, 8);
            if (node != anyMemoryNode && rangeCount < 64 && size && first + size > first) ranges[rangeCount++] = {first, first + size, node};
        } else if (entry[0] == 0 && length >= 16 && (SratReader::value(entry + 4, 4) & 1)) {
            const uint32_t domain = entry[2] | (SratReader::value(entry + 9, 3) << 8);
            const uint16_t node = SratReader::node(domain);
            if (node != anyMemoryNode && cpuCount < 256) cpus[cpuCount++] = {entry[3], node};
        } else if (entry[0] == 2 && length >= 24 && (SratReader::value(entry + 12, 4) & 1)) {
            const uint16_t node = SratReader::node(SratReader::value(entry + 4, 4));
            if (node != anyMemoryNode && cpuCount < 256) cpus[cpuCount++] = {static_cast<uint32_t>(SratReader::value(entry + 8, 4)), node};
        }
        offset += length;
    }
}
uint16_t MemoryTopology::nodeForAddress(uint64_t address) {
    for (uint32_t i = 0; i < rangeCount; ++i) if (address >= ranges[i].first && address < ranges[i].end) return ranges[i].node;
    return 0;
}
void MemoryTopology::enableCpuLocal() { __atomic_store_n(&cpuLocalReady, true, __ATOMIC_RELEASE); }
uint16_t MemoryTopology::currentNode() {
    if (!__atomic_load_n(&cpuLocalReady, __ATOMIC_ACQUIRE)) return 0;
    const uint32_t apic = cpu::getCurrent()->getApicId();
    for (uint32_t i = 0; i < cpuCount; ++i) if (cpus[i].apic == apic) return cpus[i].node;
    return 0;
}
uint16_t MemoryTopology::nodeCount() { return nodes; }
}  // namespace kernel::memory
