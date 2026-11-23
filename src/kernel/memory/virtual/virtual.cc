/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/virtual/virtual.cc
 * 操作页表，管理地址空间寿命，并按页跨地址空间复制数据。
 */

#include "kernel/boot.h"
#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/task.h"

namespace mm {

    constexpr uint64_t pageMask      = 0x000ffffffffff000ULL;
    constexpr uint64_t directMapBase = 0xffff800000000000ULL;

    // 硬件页表为一页 4096 字节、512 个表项；C++
    // 指针通过直接映射访问表，表项保存物理地址与标志。
    struct PageTable {
            uint64_t entries[512];
    };

    /*
     * 递归寻找游标之后已驻留的用户页面，供核心转储枚举。
     * 跳过不存在的页表分支、大页和设备缓存映射，不主动触发补页。
     */
    static bool findDumpPage(uint64_t tableAddress, unsigned level,
                             uint64_t base, uint64_t cursor, uint64_t& address,
                             uint64_t& flags) {
        constexpr uint64_t physicalMask = 0x000ffffffffff000ULL;
        const uint64_t* entries         = reinterpret_cast<const uint64_t*>(
            0xffff800000000000ULL + (tableAddress & physicalMask));

        // 每上一层页表，单个表项覆盖范围扩大 512
        // 倍，用层级计算该入口对应的虚拟区间。
        const unsigned shift = 12 + 9 * (level - 1);
        const uint64_t span  = 1ULL << shift;

        for (unsigned index = 0; index < (level == 4 ? 256U : 512U); ++index) {
            const uint64_t start = base + index * span;

            // 整个分支都在游标之前时直接跳过，不必再递归扫描其中页面。
            if (start + span <= cursor) {
                continue;
            }

            const uint64_t entry = entries[index];

            if ((entry & (pagePresent | pageUser)) !=
                (pagePresent | pageUser)) {
                continue;
            }

            if (entry & (pageCacheDisable | pageWriteCombining)) {
                continue;
            }

            // 已经走到普通页入口，返回的是这一页的起始地址和属性，供上层决定是否捕获。
            if (level == 1) {
                address = start;
                flags   = entry;

                return true;
            }

            // 普通用户内存不使用大页，设备大页映射也不进入这里的转储枚举。
            if (entry & (1ULL << 7)) {
                continue;
            }

            if (findDumpPage(entry, level - 1, start, cursor, address, flags)) {
                return true;
            }
        }

        return false;
    }

    static uint64_t kernelAddressSpaceRoot;
    static task::SpinLock virtualMemoryLock;

    /*
     * 从页表项中去掉标志位，再通过内核直接映射得到页表指针。
     */
    static PageTable* getTable(uint64_t address) {
        return reinterpret_cast<PageTable*>(directMapBase +
                                            (address & pageMask));
    }

    /*
     * 将内核直接映射中的页表指针换回物理地址，供硬件页表项保存。
     */
    static uint64_t getTablePhysicalAddress(const PageTable* table) {
        return reinterpret_cast<uint64_t>(table) - directMapBase;
    }

    /*
     * 分配并清零一页页表。
     * 必须先清零再挂入上级表，防止旧数据被 CPU 当成有效映射。
     */
    static PageTable* allocateTable() {
        const uint64_t physicalAddress = allocatePhysicalPage();

        if (physicalAddress == 0) {
            return nullptr;
        }

        PageTable* table = getTable(physicalAddress);

        for (uint64_t& entry : table->entries) {
            entry = 0;
        }

        return table;
    }

    /*
     * 取得下一级页表，缺失时创建。
     * 调用者持有页表锁；用户访问权限要沿每一级向上传递，不能只改最后一级。
     */
    static PageTable* getOrCreateTable(PageTable* parent, uint64_t index,
                                       uint64_t flags, uint64_t owner = 0) {
        uint64_t& entry = parent->entries[index];

        // 缺失时先申请一张全零页表，再把其物理地址写进父入口，防止硬件读到未初始化的表。
        if ((entry & pagePresent) == 0) {
            PageTable* table = allocateTable();

            if (table == nullptr) {
                return nullptr;
            }

            if (owner) {
                assignPhysicalPageOwner(getTablePhysicalAddress(table), owner);
            }

            entry = getTablePhysicalAddress(table) | pagePresent | pageWritable;
        }

        // 用户访问权限必须沿父层级一起开放，只给末级表项设用户位仍会触发权限异常。
        if ((flags & pageUser) != 0) {
            entry |= pageUser;
        }

        return getTable(entry);
    }

    /*
     * 将一个 2 MiB 大页映射拆成 512 个普通页面映射。
     * 新表准备好后才替换原入口，并保留原物理地址序列和适用权限。
     */
    static PageTable* splitLargePage(PageTable* directory, uint64_t index) {
        const uint64_t largeEntry = directory->entries[index];
        PageTable* table          = allocateTable();

        if (table == nullptr) {
            return nullptr;
        }

        constexpr uint64_t preservedFlags = 0x800000000000011fULL;

        // 去掉大页标志取得原连续物理范围，同时筛出普通页也适用的权限位。
        const uint64_t physicalBase = largeEntry & 0x000fffffffe00000ULL;
        const uint64_t flags = largeEntry & preservedFlags & ~(1ULL << 7);

        // 一次填好 512 个连续的普通页入口，原来的 2 MiB
        // 范围因此保持相同数据对应关系。
        for (uint64_t page = 0; page < 512; ++page) {
            table->entries[page] = physicalBase + page * 4096 + flags;
        }

        // 下一级表全部完成后才切换父入口，不能让 CPU 在构造到一半时看到新表。
        directory->entries[index] = getTablePhysicalAddress(table) |
                                    pagePresent | pageWritable |
                                    (largeEntry & pageUser);

        return table;
    }

    /*
     * 递归释放用户页表及其拥有的普通页面。
     * 外部对象提供的页面由对应对象管理，这里跳过，避免重复归还。
     */
    static void releaseUserPageTables(uint64_t tablePhysicalAddress,
                                      uint32_t level) {
        PageTable* table = getTable(tablePhysicalAddress);

        for (uint32_t index = 0; index < 512; ++index) {
            const uint64_t entry = table->entries[index];

            if ((entry & pagePresent) == 0) {
                continue;
            }

            // 非叶节点递归释放子表，叶节点只释放自己拥有的普通内存；外部页面由对象登记表归还。
            if (level > 1 && (entry & (1ULL << 7)) == 0) {
                releaseUserPageTables(entry & pageMask, level - 1);
            } else if ((entry & pageExternal) == 0) {
                freePhysicalPage(entry & pageMask);
            }
        }

        // 子层级都已经处理完，再释放当前这张表，递归过程中不能先拆掉仍需遍历的数据。
        freePhysicalPage(tablePhysicalAddress);
    }

    /*
     * 在锁内逐级找到或创建页表，建立一个普通页映射。
     * 拒绝覆盖已有页面；记录新表和页面的任务归属。
     */
    static bool mapPageLocked(uint64_t rootPhysicalAddress,
                              uint64_t virtualAddress, uint64_t physicalAddress,
                              uint64_t flags) {
        // 从虚拟地址逐段取出四级索引，每段 9 位，最低 12 位是页内偏移。
        const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
        const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
        const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
        const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;

        // 根页面登记的任务归属也用于新建中间页表和普通用户页，便于后续内存记账。
        PhysicalPageInfo rootInfo{};
        queryPhysicalPage(rootPhysicalAddress, rootInfo);
        PageTable* pml4 = getTable(rootPhysicalAddress);
        PageTable* pdpt =
            getOrCreateTable(pml4, pml4Index, flags, rootInfo.getOwner());

        if (pdpt == nullptr || (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
            return false;
        }

        PageTable* pd =
            getOrCreateTable(pdpt, pdptIndex, flags, rootInfo.getOwner());

        if (pd == nullptr) {
            return false;
        }

        // 目标落在已有 2 MiB
        // 大页时先拆成普通页表；否则沿普通层级取表，拒绝覆盖现有叶入口。
        PageTable* pt;

        if ((pd->entries[pdIndex] & pagePresent) != 0 &&
            (pd->entries[pdIndex] & (1ULL << 7)) != 0) {
            pt = splitLargePage(pd, pdIndex);
        } else {
            pt = getOrCreateTable(pd, pdIndex, flags, rootInfo.getOwner());
        }

        if (pt == nullptr || (pt->entries[ptIndex] & pagePresent) != 0) {
            return false;
        }

        // 所有上级表都存在且目标未占用后，最后写入真正的页面映射。
        pt->entries[ptIndex] =
            (physicalAddress & pageMask) | flags | pagePresent;

        if (rootInfo.getOwner() && (flags & pageUser) &&
            !(flags & pageExternal)) {
            assignPhysicalPageOwner(physicalAddress, rootInfo.getOwner());
        }

        return true;
    }

    /*
     * 在锁内清除一个普通页的映射入口。
     * 这里只改页表，物理页归还和地址缓存失效由调用方安排。
     */
    static bool unmapPageLocked(uint64_t rootPhysicalAddress,
                                uint64_t virtualAddress) {
        const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
        const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
        const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
        const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;

        // 沿现有页表向下查找，任何一级缺失或遇到不支持的大页都返回失败，不为解除操作新建页表。
        PageTable* pml4 = getTable(rootPhysicalAddress);

        if ((pml4->entries[pml4Index] & pagePresent) == 0) {
            return false;
        }

        PageTable* pdpt = getTable(pml4->entries[pml4Index]);

        if ((pdpt->entries[pdptIndex] & pagePresent) == 0 ||
            (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
            return false;
        }

        PageTable* pd = getTable(pdpt->entries[pdptIndex]);

        if ((pd->entries[pdIndex] & pagePresent) == 0 ||
            (pd->entries[pdIndex] & (1ULL << 7)) != 0) {
            return false;
        }

        PageTable* pt = getTable(pd->entries[pdIndex]);

        if ((pt->entries[ptIndex] & pagePresent) == 0) {
            return false;
        }

        // 只清入口，不归还物理页；调用者必须先使所有 CPU
        // 的旧翻译失效，再决定释放后备内存。
        pt->entries[ptIndex] = 0;

        return true;
    }

    /*
     * 在锁内修改普通页权限，保留物理地址以及不应被权限修改覆盖的状态位。
     */
    static bool protectPageLocked(uint64_t rootPhysicalAddress,
                                  uint64_t virtualAddress, uint64_t flags) {
        const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
        const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
        const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
        const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;

        // 只允许修改已经存在的普通页映射，查找失败时保持原表不变。
        PageTable* pml4 = getTable(rootPhysicalAddress);

        if ((pml4->entries[pml4Index] & pagePresent) == 0) {
            return false;
        }

        PageTable* pdpt = getTable(pml4->entries[pml4Index]);

        if ((pdpt->entries[pdptIndex] & pagePresent) == 0 ||
            (pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
            return false;
        }

        PageTable* pd = getTable(pdpt->entries[pdptIndex]);

        if ((pd->entries[pdIndex] & pagePresent) == 0 ||
            (pd->entries[pdIndex] & (1ULL << 7)) != 0) {
            return false;
        }

        PageTable* pt   = getTable(pd->entries[pdIndex]);
        uint64_t& entry = pt->entries[ptIndex];

        if ((entry & pagePresent) == 0) {
            return false;
        }

        // 修改权限时保留物理地址、外部所有权以及硬件访问状态，不能把页面误改成另一份分配。
        const uint64_t preserved =
            entry &
            (pageMask | pageExternal | pageLocked | (1ULL << 5) | (1ULL << 6));
        entry = preserved | flags | pagePresent;

        return true;
    }

    /*
     * 逐级查页表，将虚拟地址翻译为物理地址。
     * 兼容普通页和大页，必要时把页表标志一起返回。
     */
    static uint64_t translateLocked(uint64_t rootPhysicalAddress,
                                    uint64_t virtualAddress, uint64_t* flags) {
        const uint64_t pml4Index = (virtualAddress >> 39) & 0x1ff;
        const uint64_t pdptIndex = (virtualAddress >> 30) & 0x1ff;
        const uint64_t pdIndex   = (virtualAddress >> 21) & 0x1ff;
        const uint64_t ptIndex   = (virtualAddress >> 12) & 0x1ff;

        // 每一级都检查存在位；物理地址低位还含属性，取得下一级表时必须先去掉这些位。
        PageTable* pml4 = getTable(rootPhysicalAddress);

        if ((pml4->entries[pml4Index] & pagePresent) == 0) {
            return 0;
        }

        PageTable* pdpt = getTable(pml4->entries[pml4Index]);

        if ((pdpt->entries[pdptIndex] & pagePresent) == 0) {
            return 0;
        }

        // 这里支持 1 GiB 大页提前结束翻译，页内偏移需要取更宽的地址低位。
        if ((pdpt->entries[pdptIndex] & (1ULL << 7)) != 0) {
            if (flags != nullptr) {
                *flags = pdpt->entries[pdptIndex];
            }

            return (pdpt->entries[pdptIndex] & 0x000fffffc0000000ULL) |
                   (virtualAddress & 0x3fffffff);
        }

        PageTable* pd = getTable(pdpt->entries[pdptIndex]);

        if ((pd->entries[pdIndex] & pagePresent) == 0) {
            return 0;
        }

        // 2 MiB 大页同样在目录层结束；普通页则继续走最后一级表。
        if ((pd->entries[pdIndex] & (1ULL << 7)) != 0) {
            if (flags != nullptr) {
                *flags = pd->entries[pdIndex];
            }

            return (pd->entries[pdIndex] & 0x000fffffffe00000ULL) |
                   (virtualAddress & 0x1fffff);
        }

        PageTable* pt = getTable(pd->entries[pdIndex]);

        if ((pt->entries[ptIndex] & pagePresent) == 0) {
            return 0;
        }

        if (flags != nullptr) {
            *flags = pt->entries[ptIndex];
        }

        return (pt->entries[ptIndex] & pageMask) | (virtualAddress & 0xfff);
    }

    /*
     * 持页表锁枚举下一页可供转储的用户内存，防止遍历时页表同时被修改。
     */
    bool nextDumpPage(uint64_t root, uint64_t cursor, uint64_t& address,
                      uint64_t& flags) {
        return root && cursor < 0x800000000000ULL &&
               findDumpPage(root, 4, 0, cursor, address, flags);
    }

    /*
     * 记录并启用内核地址空间，初始化页表操作所需的锁和基础状态。
     */
    /* 在启动其他 CPU 前，统一显存的内核别名与后续用户映射的缓存类型。 */
    bool configureFramebufferCache(uint64_t physical, uint64_t size, uint64_t flags) {
        if (!size || physical >= 64ULL * 1024 * 1024 * 1024 ||
            size > 64ULL * 1024 * 1024 * 1024 - physical) {
            return false;
        }

        const uint64_t first = physical & ~4095ULL;
        const uint64_t end = (physical + size + 4095) & ~4095ULL;
        task::InterruptSpinLockGuard guard(virtualMemoryLock);
        asm volatile("wbinvd" : : : "memory");

        for (uint64_t page = first; page < end; page += 4096) {
            const uint64_t address = directMapBase + page;
            PageTable* pml4 = getTable(kernelAddressSpaceRoot);
            PageTable* pdpt = getTable(pml4->entries[(address >> 39) & 511]);
            const uint64_t pdptEntry = pdpt->entries[(address >> 30) & 511];
            if (!(pdptEntry & pagePresent) || (pdptEntry & (1ULL << 7))) {
                return false;
            }

            PageTable* directory = getTable(pdptEntry);
            const uint32_t index = (address >> 21) & 511;
            if (!(directory->entries[index] & pagePresent)) {
                return false;
            }

            PageTable* table = directory->entries[index] & (1ULL << 7)
                ? splitLargePage(directory, index) : getTable(directory->entries[index]);
            if (!table) {
                return false;
            }

            uint64_t& entry = table->entries[(address >> 12) & 511];
            entry = (entry & ~(pageCacheDisable | pageWriteCombining)) | flags;
            asm volatile("invlpg (%0)" : : "r"(address) : "memory");
        }

        asm volatile("wbinvd" : : : "memory");
        return true;
    }

    void initVir(const BootInfo* bootInfo) {
        if (bootInfo == nullptr || bootInfo->getPageTableRoot() == 0) {
            device::serialWrite("[MEMORY] missing bootstrap page table root\n");

            while (true) {
                asm volatile("cli; hlt");
            }
        }

        kernelAddressSpaceRoot = bootInfo->getPageTableRoot();
        PageTable* pml4        = getTable(kernelAddressSpaceRoot);
        pml4->entries[0]       = 0;
        asm volatile("mov %0, %%cr3"
                     :
                     : "r"(kernelAddressSpaceRoot)
                     : "memory");
        device::serialWrite(
            "[MEMORY] higher-half direct map active; identity map removed\n");
    }

    /*
     * 在内核地址空间建立一页映射，页表修改由全局页表锁保护。
     */
    bool mapPage(uint64_t virtualAddress, uint64_t physicalAddress,
                 uint64_t flags) {
        return mm::mapPageInAddressSpace(mm::getKernelAddressSpaceRoot(),
                                         virtualAddress, physicalAddress,
                                         flags);
    }

    /*
     * 在指定地址空间建立一页映射。
     * 完成后安排地址缓存更新，调用者仍负责维持页表根的寿命。
     */
    bool mapPageInAddressSpace(uint64_t rootPhysicalAddress,
                               uint64_t virtualAddress,
                               uint64_t physicalAddress, uint64_t flags) {
        if ((virtualAddress & 0xfff) != 0 || (physicalAddress & 0xfff) != 0) {
            return false;
        }

        bool mapped;
        {
            ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
            mapped = mapPageLocked(rootPhysicalAddress, virtualAddress,
                                   physicalAddress, flags);

            if (mapped) {
                asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
            }
        }

        return mapped;
    }

    /*
     * 从内核地址空间撤销一页映射，并更新相关地址缓存。
     */
    bool unmapPage(uint64_t virtualAddress) {
        return mm::unmapPageInAddressSpace(mm::getKernelAddressSpaceRoot(),
                                           virtualAddress);
    }

    /*
     * 从指定地址空间撤销一页映射。
     * 移除页表项与释放页面分别处理，避免误释放借用的外部页面。
     */
    bool unmapPageInAddressSpace(uint64_t rootPhysicalAddress,
                                 uint64_t virtualAddress) {
        if ((virtualAddress & 0xfff) != 0) {
            return false;
        }

        bool unmapped;
        {
            ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
            unmapped = unmapPageLocked(rootPhysicalAddress, virtualAddress);

            if (unmapped) {
                asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
            }
        }

        if (unmapped) {
            cpu::requestTlbShootdown(virtualAddress);
        }

        return unmapped;
    }

    /*
     * 改变指定地址空间的一页权限，并通知使用该地址空间的 CPU 丢弃旧翻译。
     */
    bool protectPageInAddressSpace(uint64_t rootPhysicalAddress,
                                   uint64_t virtualAddress, uint64_t flags) {
        if ((virtualAddress & 0xfff) != 0 || (flags & pageUser) == 0) {
            return false;
        }

        bool protectedPage;
        {
            ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
            protectedPage =
                protectPageLocked(rootPhysicalAddress, virtualAddress, flags);

            if (protectedPage) {
                asm volatile("invlpg (%0)" : : "r"(virtualAddress) : "memory");
            }
        }

        if (protectedPage) {
            cpu::requestTlbShootdown(virtualAddress);
        }

        return protectedPage;
    }

    /*
     * 批量撤销映射或修改权限。
     * 循环修改页表后统一通知 CPU，减少逐页发送跨核通知的开销。
     */
    static bool editPages(uint64_t root, uint64_t address, uint64_t size,
                          uint64_t flags, bool protect) {
        if (!root || !size || size > 64 * 4096 || ((address | size) & 4095) ||
            address > UINT64_MAX - size || (protect && !(flags & pageUser))) {
            return false;
        }

        {
            ::task::InterruptSpinLockGuard guard(virtualMemoryLock);

            // 将页表修改集中在一次持锁区间内，避免整段操作过程中被其他页表修改穿插。
            for (uint64_t offset = 0; offset < size; offset += 4096) {
                uint64_t existing = 0;

                if (!translateLocked(root, address + offset, &existing) ||
                    !(existing & pageUser) || (existing & (1ULL << 7))) {
                    return false;
                }
            }

            for (uint64_t offset = 0; offset < size; offset += 4096) {
                if (protect) {
                    protectPageLocked(root, address + offset, flags);
                } else {
                    unmapPageLocked(root, address + offset);
                }

                asm volatile("invlpg (%0)"
                             :
                             : "r"(address + offset)
                             : "memory");
            }
        }

        // 页面已更新后再处理 CPU
        // 缓存中的旧地址翻译，完成通知前后备页面仍不能复用。
        cpu::requestTlbShootdown(address, size);

        return true;
    }

    /*
     * 通过批量页表操作撤销给定区间，不在这里管理页面对象引用。
     */
    bool unmapPagesInAddressSpace(uint64_t root, uint64_t address,
                                  uint64_t size) {
        return editPages(root, address, size, 0, false);
    }

    /*
     * 通过批量页表操作更新区间权限，相关地址缓存失效由公共路径处理。
     */
    bool protectPagesInAddressSpace(uint64_t root, uint64_t address,
                                    uint64_t size, uint64_t flags) {
        return editPages(root, address, size, flags, true);
    }

    /*
     * 设置或清除某页的锁页标志。
     * 锁页标志用于阻止回收路径撤走该页，不改变其读写权限。
     */
    static bool setPageLock(uint64_t root, uint64_t address, bool locked) {
        ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        PageTable* table = getTable(root);

        for (unsigned shift = 39; shift > 12; shift -= 9) {
            const uint64_t entry = table->entries[(address >> shift) & 511];

            if (!(entry & pagePresent) || (entry & (1ULL << 7))) {
                return false;
            }

            table = getTable(entry);
        }

        uint64_t& entry = table->entries[(address >> 12) & 511];

        if ((entry & (pagePresent | pageUser)) != (pagePresent | pageUser)) {
            return false;
        }

        if (locked) {
            entry |= pageLocked;
        } else {
            entry &= ~pageLocked;
        }

        return true;
    }

    /*
     * 锁定或解锁用户区间。
     * 先校验范围和额度，再结合驻留记录与页表标记执行；可按参数只检查或允许尚未补入的页面。
     */
    int lockUserPages(task::Task& target, uint64_t address, uint64_t size,
                      bool locked, bool allowAbsent, bool checkOnly) {
        if (!size || ((address | size) & 4095) || address < 0x10000 ||
            address >= 0x800000000000ULL ||
            size > 0x800000000000ULL - address) {
            return 22;
        }

        ::task::PreemptionSpinLockGuard guard(target.addressSpace().lock());
        const uint64_t root = target.addressSpace().rootPhysicalAddress();

        if (!root) {
            return 12;
        }

        // 先检查整段页面是否可锁，包括用户权限和设备缓存属性；这一轮不修改任何页面标志。
        for (uint64_t offset = 0; offset < size; offset += 4096) {
            uint64_t flags = 0;

            if (!getPhysicalAddressInAddressSpace(root, address + offset,
                                                  &flags)) {
                if (allowAbsent ||
                    (!locked && target.addressSpace().residency().contains(
                                    address + offset))) {
                    continue;
                }

                return 12;
            }

            if (!(flags & pageUser) ||
                (flags & (pageCacheDisable | pageWriteCombining | (1ULL << 7)))) {
                return 12;
            }
        }

        // 在锁页记录副本中试做并核对额度，记录容量不足或超额时保持原状态。
        ::task::AddressSpace::Residency next =
            target.addressSpace().residency();

        if (!next.set(address, size, locked)) {
            return 12;
        }

        if (locked && next.bytes > target.memlockLimit()) {
            return 12;
        }

        // 预检查只回答能否执行，不增加锁页数量，也不修改页表。
        if (checkOnly) {
            return 0;
        }

        for (uint64_t offset = 0; offset < size; offset += 4096) {
            setPageLock(root, address + offset, locked);
        }

        // 页表标志全部处理完以后，提交与之对应的区间记录，供未来缺页和解锁使用。
        target.addressSpace().residency() = next;

        return 0;
    }

    /*
     * 按整进程锁页标志处理当前区间和未来映射，使用临时记录核对总额度，避免只完成一半就耗尽锁页限额。
     */
    int lockAllUserPages(task::Task& target, unsigned flags,
                         const uint64_t* ranges, unsigned count) {
        if (flags & ~3U) {
            return 22;
        }

        ::task::PreemptionSpinLockGuard guard(target.addressSpace().lock());
        const uint64_t root = target.addressSpace().rootPhysicalAddress();

        if (!root) {
            return 12;
        }

        // 先构造最终应锁定的区间副本，兼顾已经驻留页面和服务补充的延迟区域。
        uint64_t cursor = 0, address = 0, state = 0;
        ::task::AddressSpace::Residency next =
            target.addressSpace().residency();

        // 零标志表示解除全部锁定，临时表从空开始，后面统一清页面标记。
        if (!flags) {
            next = {};
        }

        if (flags & 1) {
            while (nextDumpPage(root, cursor, address, state)) {
                if (!next.set(address, 4096, true)) {
                    return 12;
                }

                cursor = address + 4096;
            }
        }

        if (flags & 1) {
            for (unsigned i = 0; i < count; ++i) {
                const uint64_t address = ranges[i * 2],
                               size    = ranges[i * 2 + 1];

                if (!size || ((address | size) & 4095) || address < 0x10000 ||
                    address >= 0x800000000000ULL ||
                    size > 0x800000000000ULL - address ||
                    !next.set(address, size, true)) {
                    return 12;
                }
            }
        }

        // 所有区间汇总后一次性检查额度，避免前几段已锁好、到后几段才超限。
        if ((flags & 1) && next.bytes > target.memlockLimit()) {
            return 12;
        }

        if ((flags & 1) || !flags) {
            cursor = 0;

            while (nextDumpPage(root, cursor, address, state)) {
                setPageLock(root, address, flags != 0);
                cursor = address + 4096;
            }

            target.addressSpace().residency() = next;
        }

        // 未来映射的锁页策略独立记录，让之后新建的映射也按这个策略处理。
        target.addressSpace().lockFuture() = flags & 2;

        return 0;
    }

    /*
     * 查询当前地址空间的虚拟地址对应的物理地址。
     */
    uint64_t getPhysicalAddress(uint64_t virtualAddress) {
        return mm::getPhysicalAddressInAddressSpace(
            mm::getKernelAddressSpaceRoot(), virtualAddress);
    }

    /*
     * 查询指定页表根中的地址翻译和权限，不切换当前 CPU 的地址空间。
     */
    uint64_t getPhysicalAddressInAddressSpace(uint64_t rootPhysicalAddress,
                                              uint64_t virtualAddress,
                                              uint64_t* flags) {
        ::task::InterruptSpinLockGuard guard(virtualMemoryLock);

        return translateLocked(rootPhysicalAddress, virtualAddress, flags);
    }

    /*
     * 逐页验证用户内存是否存在并满足读取或写入要求，同时检查地址范围溢出。
     */
    bool validateUserMemory(uint64_t rootPhysicalAddress, uint64_t address,
                            uint64_t size, bool writable) {
        if (size == 0) {
            return true;
        }

        const uint64_t end   = address + size - 1;
        ::task::Task* caller = task::getCurrent();

        // 只有当前特权服务自己的高地址空间适用例外；普通用户指针不能借这个接口访问内核半区。
        const bool supervisor = caller && caller->isSystemService() &&
                                caller->addressSpace().rootPhysicalAddress() ==
                                    rootPhysicalAddress &&
                                address >= 0xffff800000000000ULL;

        if (end < address || (!supervisor && end >= 0x0000800000000000ULL)) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(virtualMemoryLock);

        // 将区间首尾都换算成页，逐页检查存在、用户位和所需写权限。
        uint64_t page           = address & ~0xfffULL;
        const uint64_t lastPage = end & ~0xfffULL;

        while (true) {
            uint64_t flags = 0;

            if (translateLocked(rootPhysicalAddress, page, &flags) == 0 ||
                (!supervisor && (flags & pageUser) == 0) ||
                (writable && (flags & pageWritable) == 0)) {
                return false;
            }

            // 最后一页验证成功才返回，避免跨页缓冲区后半段藏着无效映射。
            if (page == lastPage) {
                return true;
            }

            page += 4096;
        }
    }

    /*
     * 为指定用户地址分配一个清零的新页。
     * 区分原本已有映射、成功创建以及失败，安装失败时归还新申请的页。
     */
    UserPageMapResult allocateUserPageInAddressSpace(
        uint64_t rootPhysicalAddress, uint64_t virtualAddress, uint64_t flags) {
        constexpr uint64_t pageSize       = 4096;
        constexpr uint64_t userAddressEnd = 0x0000800000000000ULL;

        // 除地址对齐和用户范围外，还拒绝同时可写又可执行的页面，防止权限组合越过接口约束。
        if (rootPhysicalAddress == 0 ||
            (virtualAddress & (pageSize - 1)) != 0 ||
            virtualAddress < pageSize || virtualAddress >= userAddressEnd ||
            (flags & pageUser) == 0 ||
            ((flags & pageWritable) != 0 && (flags & pageNoExecute) == 0)) {
            return UserPageMapResult::InvalidArgument;
        }

        const uint64_t physicalAddress = mm::allocatePhysicalPage();

        if (physicalAddress == 0) {
            return UserPageMapResult::OutOfMemory;
        }

        // 新页在映射给用户之前清零，避免把上一次分配留下的字节交给新进程。
        uint8_t* page =
            reinterpret_cast<uint8_t*>(directMapBase + physicalAddress);

        for (uint64_t index = 0; index < pageSize; ++index) {
            page[index] = 0;
        }

        // 安装失败时新物理页尚未交给其他对象，必须由本函数立即归还。
        if (!mm::mapPageInAddressSpace(rootPhysicalAddress, virtualAddress,
                                       physicalAddress, flags)) {
            mm::freePhysicalPage(physicalAddress);

            return UserPageMapResult::MappingFailed;
        }

        return UserPageMapResult::Success;
    }

    /*
     * 跨地址空间写入数据。
     * 逐页翻译目标地址，通过物理页的内核映射复制，正确处理跨页边界。
     */
    bool copyToAddressSpace(uint64_t rootPhysicalAddress, uint64_t destination,
                            const void* source, uint64_t size) {
        constexpr uint64_t pageSize       = 4096;
        constexpr uint64_t userAddressEnd = 0x0000800000000000ULL;

        if (source == nullptr || size == 0 || destination < pageSize ||
            destination >= userAddressEnd ||
            size > userAddressEnd - destination) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        const uint8_t* input = static_cast<const uint8_t*>(source);

        while (size != 0) {
            uint64_t flags = 0;

            // 每轮重新翻译目标当前页，不假定相邻虚拟页对应相邻物理页。
            const uint64_t physical =
                translateLocked(rootPhysicalAddress, destination, &flags);

            if (physical == 0 || (flags & pageUser) == 0) {
                return false;
            }

            // 本轮长度最多到当前页末尾；下一页需要重新查表后才能继续复制。
            const uint64_t pageRemaining =
                pageSize - (destination & (pageSize - 1));
            const uint64_t chunk = size < pageRemaining ? size : pageRemaining;
            uint8_t* output =
                reinterpret_cast<uint8_t*>(directMapBase + physical);

            for (uint64_t index = 0; index < chunk; ++index) {
                output[index] = input[index];
            }

            // 同时推进源、目标与剩余长度，确保跨页时不会重读或漏写一段。
            destination += chunk;
            input += chunk;
            size -= chunk;
        }

        return true;
    }

    /*
     * 跨地址空间读取数据。
     * 逐页翻译源地址并复制到内核缓冲区，不需要切换 CR3。
     */
    bool copyFromAddressSpace(void* destination, uint64_t rootPhysicalAddress,
                              uint64_t source, uint64_t size) {
        constexpr uint64_t pageSize       = 4096;
        constexpr uint64_t userAddressEnd = 0x0000800000000000ULL;

        if (destination == nullptr || size == 0 || source < pageSize ||
            source >= userAddressEnd || size > userAddressEnd - source) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        uint8_t* output = static_cast<uint8_t*>(destination);

        while (size != 0) {
            uint64_t flags = 0;

            // 在源地址空间逐页查物理地址，通过内核直接映射读取，不把另一个进程的虚拟地址直接当本地指针。
            const uint64_t physical =
                translateLocked(rootPhysicalAddress, source, &flags);

            if (physical == 0 || (flags & pageUser) == 0) {
                return false;
            }

            // 只复制当前源页剩余部分，物理上不连续的下一页由下一轮重新定位。
            const uint64_t pageRemaining = pageSize - (source & (pageSize - 1));
            const uint64_t chunk = size < pageRemaining ? size : pageRemaining;
            const uint8_t* input =
                reinterpret_cast<const uint8_t*>(directMapBase + physical);

            for (uint64_t index = 0; index < chunk; ++index) {
                output[index] = input[index];
            }

            // 完成一段后同步推进两端和剩余字节，直到整个请求范围读完。
            source += chunk;
            output += chunk;
            size -= chunk;
        }

        return true;
    }

    /*
     * 返回内核页表根物理地址，供任务启动和切换使用。
     */
    uint64_t getKernelAddressSpaceRoot() { return kernelAddressSpaceRoot; }

    /*
     * 建立新的用户地址空间根。
     * 用户半区保持为空，内核半区沿用公共映射。
     */
    uint64_t createAddressSpaceRoot() {
        ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        const uint64_t rootPhysicalAddress = mm::allocatePhysicalPage();

        if (rootPhysicalAddress == 0) {
            return 0;
        }

        PageTable* root       = getTable(rootPhysicalAddress);
        PageTable* kernelRoot = getTable(mm::getKernelAddressSpaceRoot());

        // 下半区属于新用户程序，先清空，不继承任何旧进程的用户映射。
        for (uint32_t index = 0; index < 256; ++index) {
            root->entries[index] = 0;
        }

        // 上半区沿用内核公共表，使任何任务进入内核后都能访问相同内核代码和数据。
        for (uint32_t index = 256; index < 512; ++index) {
            root->entries[index] = kernelRoot->entries[index];
        }

        return rootPhysicalAddress;
    }

    /*
     * 为 fork 复制地址空间。
     * 按页类型区分普通内存和外部对象页面，复制失败时由调用方处理目标根的清理。
     */
    bool cloneAddressSpaceRoot(uint64_t sourceRootPhysicalAddress,
                               uint64_t destinationRootPhysicalAddress) {
        if (sourceRootPhysicalAddress == 0 ||
            destinationRootPhysicalAddress == 0 ||
            sourceRootPhysicalAddress == destinationRootPhysicalAddress) {
            return false;
        }

        constexpr uint64_t largePage = 1ULL << 7;
        constexpr uint64_t preservedFlags =
            pageWritable | pageUser | pageWriteCombining | pageCacheDisable |
            pageGlobal | pageExternal | pageNoExecute;
        ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        PageTable* sourcePml4 = getTable(sourceRootPhysicalAddress);

        // 只遍历用户半区，内核公共映射已在目标根创建时继承，不需要重复复制。
        for (uint32_t pml4Index = 0; pml4Index < 256; ++pml4Index) {
            const uint64_t pml4Entry = sourcePml4->entries[pml4Index];

            if ((pml4Entry & pagePresent) == 0) {
                continue;
            }

            PageTable* sourcePdpt = getTable(pml4Entry);

            for (uint32_t pdptIndex = 0; pdptIndex < 512; ++pdptIndex) {
                const uint64_t pdptEntry = sourcePdpt->entries[pdptIndex];

                if ((pdptEntry & pagePresent) == 0) {
                    continue;
                }

                if ((pdptEntry & largePage) != 0) {
                    return false;
                }

                PageTable* sourcePd = getTable(pdptEntry);

                for (uint32_t pdIndex = 0; pdIndex < 512; ++pdIndex) {
                    const uint64_t pdEntry = sourcePd->entries[pdIndex];

                    if ((pdEntry & pagePresent) == 0) {
                        continue;
                    }

                    if ((pdEntry & largePage) != 0) {
                        return false;
                    }

                    PageTable* sourcePt = getTable(pdEntry);

                    for (uint32_t ptIndex = 0; ptIndex < 512; ++ptIndex) {
                        const uint64_t entry = sourcePt->entries[ptIndex];

                        if ((entry & pagePresent) == 0) {
                            continue;
                        }

                        // 把四级索引拼回完整用户页地址，保证目标映射落在与父进程相同的位置。
                        const uint64_t virtualAddress =
                            (static_cast<uint64_t>(pml4Index) << 39) |
                            (static_cast<uint64_t>(pdptIndex) << 30) |
                            (static_cast<uint64_t>(pdIndex) << 21) |
                            (static_cast<uint64_t>(ptIndex) << 12);
                        const uint64_t sourcePhysical = entry & pageMask;
                        uint64_t destinationPhysical  = sourcePhysical;
                        // 只为私有普通内存分配新页；外部对象页面必须在子进程可运行前由其登记表保留引用。
                        if ((entry & pageExternal) == 0) {
                            destinationPhysical = mm::allocatePhysicalPage();

                            if (destinationPhysical == 0) {
                                return false;
                            }

                            const uint8_t* source =
                                reinterpret_cast<const uint8_t*>(
                                    directMapBase + sourcePhysical);
                            uint8_t* destination = reinterpret_cast<uint8_t*>(
                                directMapBase + destinationPhysical);

                            // 对普通私有页面复制完整 4 KiB
                            // 内容，复制通过内核直接映射进行，不切换当前任务页表。
                            size_t words = 4096 / 8;
                            asm volatile("rep movsq"
                                         : "+D"(destination), "+S"(source),
                                           "+c"(words)
                                         :
                                         : "memory");
                        }

                        // 安装失败时先归还本轮新建私有页；此前已安装的目标页面仍由调用者销毁目标地址空间时清理。
                        if (!mapPageLocked(destinationRootPhysicalAddress,
                                           virtualAddress, destinationPhysical,
                                           entry & preservedFlags)) {
                            if ((entry & pageExternal) == 0) {
                                mm::freePhysicalPage(destinationPhysical);
                            }

                            return false;
                        }
                    }
                }
            }
        }

        return true;
    }

    /*
     * 销毁用户地址空间。
     * 保护内核公共根，递归释放私有用户页表，再归还顶层根页面。
     */
    void destroyAddressSpaceRoot(uint64_t rootPhysicalAddress) {
        if (rootPhysicalAddress == 0 ||
            rootPhysicalAddress == mm::getKernelAddressSpaceRoot()) {
            return;
        }

        ::task::InterruptSpinLockGuard guard(virtualMemoryLock);
        PageTable* root = getTable(rootPhysicalAddress);

        for (uint32_t index = 0; index < 256; ++index) {
            const uint64_t entry = root->entries[index];

            if ((entry & pagePresent) != 0) {
                releaseUserPageTables(entry & pageMask, 3);
            }
        }

        mm::freePhysicalPage(rootPhysicalAddress);
    }

    /*
     * 把 CPU 切换到指定页表根，并同步维护当前地址空间记录。
     */
    void switchAddressSpace(uint64_t rootPhysicalAddress) {
        if (rootPhysicalAddress == 0) {
            rootPhysicalAddress = mm::getKernelAddressSpaceRoot();
        }

        uint64_t currentRoot;
        asm volatile("movq %%cr3, %0" : "=r"(currentRoot));

        if ((currentRoot & pageMask) != (rootPhysicalAddress & pageMask)) {
            asm volatile("movq %0, %%cr3"
                         :
                         : "r"(rootPhysicalAddress)
                         : "memory");
        }
    }

}  // namespace mm
