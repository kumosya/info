#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <new>
#include <cstring>
#include "kernel/page.h"
#include "kernel/mm.h"
namespace {
constexpr size_t pages = 8192;
unsigned char* arena;
kernel::memory::PageFrame frames[pages];
kernel::memory::BuddyAllocator buddy;
thread_local uint32_t hostCpuId;
}
namespace kernel::interrupt { uint64_t disable() { return 0; } void restore(uint64_t) {} }
namespace kernel::cpu { uint32_t getCurrentId() { return hostCpuId; } }
namespace kernel::serial { void serialWrite(const char*) {} }
namespace kernel::memory {
uint64_t allocatePhysicalBlock(uint32_t order, uint64_t owner, uint16_t node, bool strict, uint64_t limit) { return buddy.allocate(order, owner, node, strict, limit); }
bool retainPhysicalBlock(uint64_t address, uint64_t owner) { return buddy.retain(address, owner); }
bool releasePhysicalBlock(uint64_t address, uint32_t order, uint64_t owner) { return buddy.release(address, order, owner); }
bool queryPhysicalPage(uint64_t address, PhysicalPageInfo& info) { return buddy.query(address, info); }
void KernelMapping::clear(uint64_t physical, uint64_t bytes) { std::memset(arena + physical, 0, bytes); }
void* KernelMapping::map(uint64_t physical, uint32_t order) { return physical + (4096ULL << order) <= pages * 4096 ? arena + physical : nullptr; }
uint64_t KernelMapping::physical(const void* address) {
    const auto value = reinterpret_cast<uint64_t>(address), base = reinterpret_cast<uint64_t>(arena);
    return value >= base && value - base < pages * 4096 ? value - base : 0;
}
}
int main() {
    using namespace kernel::memory;
    arena = static_cast<unsigned char*>(std::aligned_alloc(4096, pages * 4096)); assert(arena);
    buddy.initialize(frames, pages); assert(buddy.addRange(256, pages - 256, 0));
    const auto initial = buddy.freePages();
    ObjectCache cache; cache.initialize(80);
    std::vector<void*> objects;
    for (unsigned i = 0; i < 1500; ++i) { auto* value = cache.allocate(); assert(value && !(reinterpret_cast<uint64_t>(value) & 15)); objects.push_back(value); }
    assert(!cache.release(static_cast<unsigned char*>(objects[0]) + 1));
    for (auto* value : objects) assert(cache.release(value));
    cache.shrink(); assert(buddy.freePages() == initial);
    alignas(16) unsigned char poolStorage[32 * 80];
    FixedPool pool; assert(pool.initialize(poolStorage, sizeof(poolStorage), 31));
    objects.clear(); for (unsigned i = 0; i < 80; ++i) { auto* value = pool.allocate(); assert(value); objects.push_back(value); }
    assert(!pool.allocate() && !pool.available());
    assert(pool.release(objects[0]) && !pool.release(objects[0]));
    for (unsigned i = 1; i < objects.size(); ++i) assert(pool.release(objects[i]));
    assert(pool.available() == 80);
    kernel::heap::init(); PerCpuObjectCache::enableCpuLocal();
    void* metadata = kernel::heap::kernelAllocate(sizeof(PageObject)); assert(metadata);
    auto* pageObject = new (metadata) PageObject(1, 42, 0);
    assert(pageObject->valid() && pageObject->size() == 8192);
    for (size_t i = 0; i < pageObject->size(); ++i) assert(arena[pageObject->physical() + i] == 0);
    pageObject->retain();
    PhysicalPageInfo pageInfo{}; assert(queryPhysicalPage(pageObject->physical(), pageInfo) && pageInfo.references == 2);
    pageObject->release();
    assert(queryPhysicalPage(pageObject->physical(), pageInfo) && pageInfo.references == 1);
    pageObject->release(); assert(buddy.pagesOwned(42) == 0);
    metadata = kernel::heap::kernelAllocate(sizeof(PageObject)); assert(metadata);
    pageObject = new (metadata) PageObject(13, 42, 0);
    assert(!pageObject->valid() && pageObject->size() == 0); pageObject->release();
    metadata = kernel::heap::kernelAllocate(sizeof(PageObject)); assert(metadata);
    std::vector<uint64_t> exhausted;
    while (auto physical = buddy.allocate(0)) exhausted.push_back(physical);
    pageObject = new (metadata) PageObject(0, 42, 0);
    assert(!pageObject->valid()); pageObject->release();
    for (auto physical : exhausted) assert(buddy.release(physical, 0));
    std::vector<std::thread> workers;
    for (unsigned n = 0; n < 4; ++n) workers.emplace_back([n] {
        hostCpuId = n;
        for (unsigned i = 0; i < 800; ++i) {
            const size_t size = (i * 997) % 30000 + 1;
            auto* value = static_cast<unsigned char*>(kernel::heap::kernelAllocate(size)); assert(value);
            value[0] = 0x42; value[size - 1] = 0x33;
            assert(value[0] == (size == 1 ? 0x33 : 0x42));
            kernel::heap::kernelFree(value);
        }
    });
    for (auto& worker : workers) worker.join();
    kernel::heap::reclaimCaches(); assert(buddy.freePages() == initial);
    std::free(arena);
    std::puts("kernel memory: slab, pool, per-CPU magazines, large buddy heap, PageObject constructor/failure/reference handling and cache reclaim passed");
}
