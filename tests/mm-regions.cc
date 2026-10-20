#include <cassert>
#include <cstdio>
#include "../src/mm/page.h"
int main() {
    mm::RegionTable table;
    mm::Region region{}; region.task = 12; region.address = 0x50000000; region.size = 8 * 4096; region.backing = 7;
    region.protection = region.maximumProtection = 3;
    assert(table.insert(region)); assert(!table.insert(region));
    assert(!mm::RegionTable::validRange(UINT64_MAX - 4095, 8192));
    assert(!table.protect(12, region.address, region.size, 7));
    assert(table.protect(12, region.address + 4096, 2 * 4096, 1));
    assert(table.references(7) == 3 && table.find(12, region.address + 8192)->protection == 1);
    assert(table.protect(12, region.address, region.size, 3)); assert(table.references(7) == 1);
    assert(table.remove(12, region.address + 2 * 4096, 2 * 4096));
    assert(table.references(7) == 2 && !table.find(12, region.address + 8192));
    assert(table.find(12, region.address + 4 * 4096)->offset == 4 * 4096);
    assert(table.findGap(12, 8192, region.address) == region.address + 8192);
    assert(!table.protect(12, region.address, region.size, 1));
    table.removeTask(12); assert(!table.references(7));
    region.size = 4 * 4096;
    for (size_t i = 0; i < mm::RegionTable::capacity; ++i) {
        region.address = 0x50000000 + i * 8 * 4096; region.backing = i + 1; assert(table.insert(region));
    }
    assert(!table.remove(12, 0x50001000, 4096));
    assert(!table.protect(12, 0x50001000, 4096, 1));
    assert(table.find(12, 0x50001000)->size == 4 * 4096);
    std::puts("MM VMA: gaps, overlap/overflow rejection, split, merge, protection bounds and metadata exhaustion passed");
}
