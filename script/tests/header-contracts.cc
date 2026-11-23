/* 实例化模板并验证资源所有权约束，补充仅包含头文件无法覆盖的检查。 */
#include <cassert>
#include <thread>
#include "kernel/atomic.h"
#include "kernel/device.h"

static_assert(!__is_constructible(device::MmioRegion, const device::MmioRegion&));
static_assert(!__is_constructible(device::DmaBuffer, const device::DmaBuffer&));
static_assert(!__is_constructible(device::InterruptBinding, const device::InterruptBinding&));
static_assert(!__is_constructible(device::ResourceDomain, const device::ResourceDomain&));
static_assert(!__is_constructible(Atomic<int>, const Atomic<int>&));

class Pair {
public:
    uint16_t first, second;
};

int main()
{
    // 通用 compare_exchange 应能处理受支持大小的平凡类型，而非只接受整数和指针。
    Atomic<Pair> pair(Pair{1, 2});
    Pair expected{1, 2};
    assert(pair.compareExchange(expected, Pair{3, 4}, MemoryOrder::AcquireRelease));
    assert(pair.load().second == 4);
    assert(!pair.compareExchange(expected, Pair{5, 6}));
    assert(expected.first == 3 && expected.second == 4);

    Atomic<uint64_t> count(0);
    std::thread workers[4];
    for (std::thread& worker : workers)
    {
        worker = std::thread([&count] {
            for (unsigned index = 0; index < 20000; ++index)
            {
                uint64_t value = count.load(MemoryOrder::Relaxed);
                while (!count.compareExchange(value, value + 1, MemoryOrder::Release)) {}
            }
        });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    assert(count.load(MemoryOrder::Acquire) == 80000);
    return 0;
}
