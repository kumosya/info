/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/physical/physical.cc
 * 物理页分配器
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"

namespace mm {

    constexpr uint64_t directMap = 0xffff800000000000ULL;
    static PhyAlloc physicalAllocator;

    /*
     * 让整个内核共享同一套物理页状态和伙伴分配器，避免重复初始化。
     * 该对象在内核启动时初始化，之后所有物理页分配请求都通过它转发给伙伴分配器。
     */
    PhyAlloc& phyAlloc() {
        return physicalAllocator;
    }

    /*
     * 检查启动位图中某一页是否可用。
     * 该位图用于初始化阶段筛选可交给伙伴分配器的页面。
     */
    bool PhyAlloc::test(uint64_t page) const {
        return (available[page / 8] & (1U << (page % 8))) != 0;
    }

    /*
     * 修改启动位图中一页的可用标记，并同步维护可用页计数。
     */
    void PhyAlloc::set(uint64_t page, bool usable) {
        if (usable) {
            available[page / 8] |= 1U << (page % 8);
        } else {
            available[page / 8] &= ~(1U << (page % 8));
        }
    }

    /*
     * 把物理地址区间换算成页号范围，并逐页设置可用性。
     * 地址范围采用左闭右开语义：[first, first + size)
     */
    void PhyAlloc::range(uint64_t first, uint64_t size, bool usable) {
        if (!size || first >= limit) {
            return;
        }

        uint64_t end = first + size;

        if (end < first || end > limit) {
            end = limit;
        }

        const uint64_t start = usable ? (first + 0xfff) / 0x1000 : first / 0x1000;
        const uint64_t last  = usable ? end / 0x1000 : (end + 0xfff) / 0x1000;

        for (uint64_t page = start; page < last; ++page) {
            set(page, usable);
        }
    }

    /*
     * 根据引导内存表建立物理页管理数据。
     * 排除内核、引导数据和元数据占用的范围，再按内存节点把可用连续区间加入伙伴分配器。
     */
    void PhyAlloc::init(const BootInfo* boot) {
        // 先让 Bitmap 全部清零
        for (uint8_t& byte : available) {
            byte = 0;
        }

        highestAddress = 0;

        // 初始化内存拓扑，读取固件的 SRAT 表，收集内存区间和 CPU 所属节点。
        memoryTopology().init(boot);

        // 遍历内存映射表，将可用内存区间加入物理页管理器
        for (uint32_t i = 0; i < boot->getMemoryMapCount(); ++i) {
            const MemoryRegion& region = boot->getMemoryMap()[i];

            if (region.getType() != memoryAvailable ||
                region.getBase() >= limit) {
                continue;
            }

            // 设为可用
            range(region.getBase(), region.getLength(), true);

            // 更新 highestAddress
            const uint64_t end = region.getBase() + region.getLength();
            const uint64_t bounded =
                end < region.getBase() || end > limit ? limit : end;

            if (bounded > highestAddress) {
                highestAddress = bounded;
            }
        }

        // 固件明确保留的区间优先于与它重叠的可用区间。
        for (uint32_t i = 0; i < boot->getMemoryMapCount(); ++i) {
            if (boot->getMemoryMap()[i].getType() != memoryAvailable) {
                range(boot->getMemoryMap()[i].getBase(),
                      boot->getMemoryMap()[i].getLength(), false);
            }
        }

        // 低地址固件区、启动代码和页表、引导信息、模块及显示缓冲区都必须排除，即使固件把它们标为可用。
        range(0, 1024 * 1024, false);
        range(boot->getBootstrapStart(),
              boot->getBootstrapEnd() - boot->getBootstrapStart(), false);
        range(boot->getMultibootAddress(), boot->getMultibootSize(), false);

        for (uint32_t i = 0; i < boot->getModuleCount(); ++i) {
            range(boot->getModules()[i].getStart(),
                  boot->getModules()[i].getEnd() -
                      boot->getModules()[i].getStart(),
                  false);
        }

        range(boot->getFramebuffer().getAddress(),
              static_cast<uint64_t>(boot->getFramebuffer().getPitch()) *
                  boot->getFramebuffer().getHeight(),
              false);
        const uint32_t count = highestAddress / 4096;
        
        // 每页的管理记录也需要物理内存；先用启动位图找到并保留这段空间，再建立空闲表，防止分配器覆盖自己的记录。
        const uint64_t metadataPages =
            (static_cast<uint64_t>(count) * sizeof(PageFrame) + 4095) / 4096;

        uint64_t run = 0, metadata = 0;

        // 在仍可用的页中找出足够长的连续区间存管理数组，这段空间不能再提供给普通申请。
        for (uint32_t page = 256; page < count; ++page) {
            run = test(page) ? run + 1 : 0;

            if (run == metadataPages) {
                metadata = page + 1 - run;
                break;
            }
        }

        if (!metadata || !metadataPages) {
            device::serialWrite(
                "[MEMORY] insufficient page metadata storage\n");
            cpu::halt();
        }

        range(metadata * 4096, metadataPages * 4096, false);

        // 管理数组位置确认并从可用区间排除以后，才允许初始化正式分配器和填充空闲块。
        BuddyAlloc::init(
            reinterpret_cast<PageFrame*>(directMap + metadata * 4096), count);
        
        // 只把同节点的连续可用页面归为一段；保留空洞和节点变化都必须截断，伙伴合并不能跨越这些边界。
        for (uint32_t page = 256; page < count;) {
            if (!test(page)) {
                ++page;
                continue;
            }

            const uint32_t first = page;
            const uint16_t node  = memoryTopology().nodeForAddress(
                static_cast<uint64_t>(page) * 4096);

            while (page < count && test(page) &&
                   memoryTopology().nodeForAddress(static_cast<uint64_t>(page) *
                                                   4096) == node) {
                ++page;
            }

            addRange(first, page - first, node);
        }

        /*
        device::serialWrite("[MEMORY] NUMA buddy initialized, free pages=");
        device::serialWriteDecimal(freePages());
        device::serialWrite(" nodes=");
        device::serialWriteDecimal(memoryTopology().nodeCount());
        device::serialWrite("\n");
        */
    }

    /*
     * 把连续页分配请求交给伙伴分配器，保留调用方指定的大小、归属、节点和物理地址上限。
     */
    uint64_t PhyAlloc::allocate(uint32_t order, uint64_t owner, uint16_t node,
                                bool strict, uint64_t maximum) {
        return BuddyAlloc::allocate(
            order, owner,
            node == anyMemoryNode ? memoryTopology().currentNode() : node,
            strict, maximum);
    }

    /*
     * 初始化物理内存分配
     */
    void initPhy(const BootInfo* boot) {
        phyAlloc().init(boot); 
    }

    /*
     * 申请指定大小和归属的物理页块。
     * 返回物理起始地址，零表示没有满足条件的空间。
     */
    uint64_t allocatePhysicalBlock(uint32_t order, uint64_t owner,
                                   uint16_t node, bool strict,
                                   uint64_t maximum) {
        return phyAlloc().allocate(order, owner, node, strict, maximum);
    }

    /*
     * 申请一个普通物理页，返回物理地址，零表示没有可用页。
     */
    uint64_t allocatePhysicalPage() {
        return allocatePhysicalBlock(0);
    }

    /*
     * 归还一个普通物理页。
     * 底层仍检查页面是否为合法分配，避免任意地址进入空闲池。
     */
    bool freePhysicalPage(uint64_t address) {
        return phyAlloc().release(address, 0, UINT64_MAX);
    }

    /*
     * 增加指定物理块的引用，把实际状态检查交给伙伴分配器。
     */
    bool retainPhysicalBlock(uint64_t address, uint64_t owner) {
        return phyAlloc().retain(address, owner);
    }

    /*
     * 按原分配大小和所有者归还物理块引用，返回底层检查是否通过。
     */
    bool releasePhysicalBlock(uint64_t address, uint32_t order,
                              uint64_t owner) {
        return phyAlloc().release(address, order, owner);
    }

    /*
     * 将物理页面对应的分配块登记到指定任务名下，供占用统计使用。
     */
    bool assignPhysicalPageOwner(uint64_t address, uint64_t owner) {
        return phyAlloc().assignOwner(address, owner);
    }

    /*
     * 查询物理地址对应的分配信息，不改变引用和所有权。
     */
    bool queryPhysicalPage(uint64_t address, PhysicalPageInfo& info) {
        return phyAlloc().query(address, info);
    }

    /*
     * 把指定所有者的页数换算成字节数，供任务内存统计使用。
     */
    uint64_t getOwnedMemoryBytes(uint64_t owner) {
        return phyAlloc().ownedBytes(owner);
    }

    /*
     * 返回初始化时统计出的物理内存总量。
     */
    uint64_t getTotalMemoryBytes() { return phyAlloc().totalBytes(); }

    /*
     * 将当前空闲页数换算成字节数。
     */
    uint64_t getFreeMemoryBytes() { return phyAlloc().freeBytes(); }

}  // namespace mm
