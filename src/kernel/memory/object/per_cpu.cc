/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/per_cpu.cc
 * 每个 CPU 暂存空闲对象，批量与公共缓存交换。
 */

#include "kernel/cpu.h"
#include "kernel/mm.h"

namespace mm {

    static bool localReady;

    /*
     * 在 CPU 局部状态可用后启用各 CPU 的对象缓存。
     * 启动早期仍使用默认缓存位置。
     */
    void enableCpuObjectCaches() {
        __atomic_store_n(&localReady, true, __ATOMIC_RELEASE);
    }

    /*
     * 取得当前 CPU 的缓存编号。
     * 局部缓存尚未启用时返回启动阶段使用的编号。
     */
    uint32_t PerCpuObjectCache::currentCpu() {
        return __atomic_load_n(&localReady, __ATOMIC_ACQUIRE)
                   ? cpu::getCurrentId() % cpuCapacity
                   : 0;
    }

    /*
     * 初始化共享对象缓存和每个 CPU 的暂存区，所有暂存区开始时都为空。
     */
    void PerCpuObjectCache::init(size_t objectSize) {
        cache_.init(objectSize);

        for (Magazine& magazine : magazines_) {
            magazine.lock.init();
            magazine.count = 0;
        }
    }

    /*
     * 优先从当前 CPU 的暂存区取对象；没有存货时从共享缓存补充。
     * 操作期间限制抢占，避免执行到一半换到另一个 CPU。
     */
    void* PerCpuObjectCache::allocate() {
        {
            // 读取 CPU 编号和访问该 CPU
            // 暂存区要连续完成，暂时关中断避免中途被切走。
            const uint64_t flags = cpu::disableInterrupts();
            Magazine& magazine   = magazines_[currentCpu()];
            magazine.lock.lock();
            void* object = magazine.take();
            magazine.lock.unlock();
            cpu::restoreInterrupts(flags);

            if (object) {
                return object;
            }
        }

        // 本地没有对象时先放掉本地锁，再向公共缓存申请，避免把两层缓存锁长期叠在一起。
        return cache_.allocate();
    }

    /*
     * 把对象暂存到当前 CPU。
     * 暂存区满时把本次对象直接归还共享缓存，以限制本地保留量。
     */
    void PerCpuObjectCache::release(void* object) {
        if (!object) {
            return;
        }

        // 只在暂存区的入栈操作期间关中断并持锁，保证 count 和槽位对应。
        const uint64_t flags = cpu::disableInterrupts();
        Magazine& magazine   = magazines_[currentCpu()];
        magazine.lock.lock();

        // 还有空槽就保存在本 CPU，下一次小对象申请可以直接复用。
        if (magazine.count < magazineCapacity) {
            magazine.objects[magazine.count++] = object;
            magazine.lock.unlock();
            cpu::restoreInterrupts(flags);

            return;
        }

        magazine.lock.unlock();
        cpu::restoreInterrupts(flags);

        // 暂存区已满，本次对象直接归还公共缓存；到这里已经释放本地锁并恢复中断。
        cache_.release(object);
    }

    /*
     * 把所有 CPU 暂存的对象归还共享缓存，再尝试释放空闲页。
     * 用于内存紧张时主动回收缓存。
     */
    uint64_t PerCpuObjectCache::drain() {
        // 逐个锁住暂存区，将其中对象全部交回公共缓存，空闲页才能被公共缓存识别。
        for (Magazine& magazine : magazines_) {
            ::task::InterruptSpinLockGuard guard(magazine.lock);

            while (magazine.count) {
                cache_.release(magazine.objects[--magazine.count]);
            }
        }

        // 所有本地暂存引用都交回以后，再释放真正全空的页面。
        return cache_.shrink();
    }

}  // namespace mm
