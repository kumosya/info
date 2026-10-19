// Exercise the real buddy allocator without privileged CPU instructions.
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
#include <random>
#include "kernel/page.h"
namespace kernel::interrupt { uint64_t disable() { return 0; } void restore(uint64_t) {} }
int main() {
    using namespace kernel::memory;
    std::vector<PageFrame> frames(4096);
    BuddyAllocator buddy;
    buddy.initialize(frames.data(), frames.size());
    assert(buddy.addRange(256, 1792, 0));
    assert(buddy.addRange(2048, 2048, 1));
    assert(!buddy.addRange(256, 1, 0));
    const uint64_t initial = buddy.freePages();
    auto block = buddy.allocate(5, 91, 1, true);
    assert(block && !(block & ((4096ULL << 5) - 1)) && block >= 2048 * 4096ULL);
    PhysicalPageInfo info{};
    assert(buddy.query(block + 4096, info) && info.owner == 91 && info.node == 1 && info.order == 5 && info.references == 1);
    assert(!buddy.release(block + 4096, 0));
    assert(!buddy.release(block, 5, 92));
    assert(buddy.retain(block, 91));
    assert(buddy.release(block, 5, 91) && buddy.freePages() == initial - 32);
    assert(buddy.release(block, 5, 91) && buddy.freePages() == initial);
    assert(!buddy.release(block, 5, 91));
    assert(!buddy.allocate(1, 0, 3, true));
    block = buddy.allocate(3, 0, 0, true, 2048 * 4096);
    assert(block && block + (4096ULL << 3) <= 2048 * 4096);
    assert(buddy.assignOwner(block, 44));
    assert(!buddy.assignOwner(block, 45));
    assert(buddy.pagesOwned(44) == 8);
    assert(buddy.release(block, 3, 44));
    std::vector<std::thread> workers;
    for (unsigned n = 0; n < 4; ++n) workers.emplace_back([&, n] {
        std::minstd_rand random(n + 1);
        std::vector<std::pair<uint64_t, unsigned>> owned;
        for (unsigned i = 0; i < 1500; ++i) {
            if (owned.empty() || random() % 2) {
                const unsigned order = random() % 6;
                const auto address = buddy.allocate(order, n + 1, n % 2);
                if (address) owned.emplace_back(address, order);
            } else {
                auto [address, order] = owned.back(); owned.pop_back();
                assert(buddy.release(address, order, n + 1));
            }
        }
        for (auto [address, order] : owned) assert(buddy.release(address, order, n + 1));
    });
    for (auto& worker : workers) worker.join();
    assert(buddy.freePages() == initial);
    std::vector<uint64_t> all;
    while (auto page = buddy.allocate(0)) all.push_back(page);
    assert(all.size() == initial && buddy.freePages() == 0);
    for (auto page : all) assert(buddy.release(page, 0));
    assert(buddy.freePages() == initial);
    assert(buddy.allocate(11, 12, 1, true) == 2048 * 4096ULL);
    std::puts("kernel memory: buddy splitting/coalescing, ownership, references, NUMA selection, exhaustion and concurrency passed");
}
