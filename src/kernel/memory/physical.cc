#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/cpu.h"

namespace kernel::memory {
namespace {
constexpr uint64_t limit = 64ULL * 1024 * 1024 * 1024;
constexpr uint64_t directMap = 0xffff800000000000ULL;
uint8_t available[limit / 4096 / 8];
uint64_t highestAddress;
BuddyAllocator allocator;
class BootPages {
   public:
    static bool test(uint64_t page) { return (available[page / 8] & (1U << (page % 8))) != 0; }
    static void set(uint64_t page, bool usable) {
        if (usable) available[page / 8] |= 1U << (page % 8);
        else available[page / 8] &= ~(1U << (page % 8));
    }
    static void range(uint64_t first, uint64_t size, bool usable) {
        if (!size || first >= limit) return;
        uint64_t end = first + size;
        if (end < first || end > limit) end = limit;
        const uint64_t start = usable ? (first + 4095) / 4096 : first / 4096;
        const uint64_t last = usable ? end / 4096 : (end + 4095) / 4096;
        for (uint64_t page = start; page < last; ++page) set(page, usable);
    }
};
}
void initPhy(const BootInfo* boot) {
    for (auto& byte : available) byte = 0;
    highestAddress = 0;
    MemoryTopology::init(boot);
    for (uint32_t i = 0; i < boot->memoryMapCount; ++i) {
        const auto& region = boot->memoryMap[i];
        if (region.type != memoryAvailable || region.base >= limit) continue;
        BootPages::range(region.base, region.length, true);
        const uint64_t end = region.base + region.length;
        const uint64_t bounded = end < region.base || end > limit ? limit : end;
        if (bounded > highestAddress) highestAddress = bounded;
    }
    // Firmware holes win over overlapping usable descriptors.
    for (uint32_t i = 0; i < boot->memoryMapCount; ++i) if (boot->memoryMap[i].type != memoryAvailable)
        BootPages::range(boot->memoryMap[i].base, boot->memoryMap[i].length, false);
    BootPages::range(0, 1024 * 1024, false);
    BootPages::range(boot->bootstrapStart, boot->bootstrapEnd - boot->bootstrapStart, false);
    BootPages::range(boot->multibootAddress, boot->multibootSize, false);
    for (uint32_t i = 0; i < boot->moduleCount; ++i)
        BootPages::range(boot->modules[i].start, boot->modules[i].end - boot->modules[i].start, false);
    BootPages::range(boot->framebuffer.address, static_cast<uint64_t>(boot->framebuffer.pitch) * boot->framebuffer.height, false);
    const uint32_t count = highestAddress / 4096;
    const uint64_t metadataPages = (static_cast<uint64_t>(count) * sizeof(PageFrame) + 4095) / 4096;
    uint64_t run = 0, metadata = 0;
    for (uint32_t page = 256; page < count; ++page) {
        run = BootPages::test(page) ? run + 1 : 0;
        if (run == metadataPages) { metadata = page + 1 - run; break; }
    }
    if (!metadata || !metadataPages) { serial::serialWrite("[MEMORY] insufficient page metadata storage\n"); cpu::halt(); }
    BootPages::range(metadata * 4096, metadataPages * 4096, false);
    allocator.init(reinterpret_cast<PageFrame*>(directMap + metadata * 4096), count);
    for (uint32_t page = 256; page < count;) {
        if (!BootPages::test(page)) { ++page; continue; }
        const uint32_t first = page;
        const uint16_t node = MemoryTopology::nodeForAddress(static_cast<uint64_t>(page) * 4096);
        while (page < count && BootPages::test(page) && MemoryTopology::nodeForAddress(static_cast<uint64_t>(page) * 4096) == node) ++page;
        allocator.addRange(first, page - first, node);
    }
    serial::serialWrite("[MEMORY] NUMA buddy initialized, free pages=");
    serial::serialWriteDecimal(allocator.freePages());
    serial::serialWrite(" nodes="); serial::serialWriteDecimal(MemoryTopology::nodeCount()); serial::serialWrite("\n");
}
uint64_t allocatePhysicalBlock(uint32_t order, uint64_t owner, uint16_t node, bool strict, uint64_t maximum) {
    return allocator.allocate(order, owner, node == anyMemoryNode ? MemoryTopology::currentNode() : node, strict, maximum);
}
uint64_t allocatePhysicalPage() { return allocatePhysicalBlock(0); }
void freePhysicalPage(uint64_t address) { allocator.release(address, 0); }
bool retainPhysicalBlock(uint64_t address, uint64_t owner) { return allocator.retain(address, owner); }
bool releasePhysicalBlock(uint64_t address, uint32_t order, uint64_t owner) { return allocator.release(address, order, owner); }
bool assignPhysicalPageOwner(uint64_t address, uint64_t owner) { return allocator.assignOwner(address, owner); }
bool queryPhysicalPage(uint64_t address, PhysicalPageInfo& info) { return allocator.query(address, info); }
uint64_t getOwnedMemoryBytes(uint64_t owner) { return allocator.pagesOwned(owner) * 4096; }
uint64_t getTotalMemoryBytes() { return highestAddress; }
uint64_t getFreeMemoryBytes() { return allocator.freePages() * 4096; }
}  // namespace kernel::memory
