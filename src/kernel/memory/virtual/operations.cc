/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/virtual/operations.cc
 * 统一检查用户映射范围，协调页表修改、记录拆分和物理页归还。
 */

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

namespace mm {

    // 这里的立即映射操作只接受对齐且不超过 64 MiB
    // 的范围；区域、文件来源和延迟分配策略由用户态内存服务管理。
    constexpr uint64_t pageSize = 4096, maximumMapSize = 64ULL * 1024 * 1024,
                       directMapBase = 0xffff800000000000ULL;

    /*
     * 将系统调用错误码转换为统一的无符号返回表示。
     */
    static uint64_t error(::task::SyscallError value) {
        return static_cast<uint64_t>(static_cast<int64_t>(value));
    }

    /*
     * 检查用户地址范围是否对齐、是否溢出，以及是否完全位于允许的用户空间。
     */
    static bool validUserRange(uint64_t address, uint64_t size) {
        return size && address >= 0x10000 && address + size > address &&
               address + size <= 0x0000800000000000ULL;
    }

    /*
     * 为任务建立普通匿名页面映射。
     * 先验证参数，再分配并安装各页；后续失败时撤销本次已经建立的映射。
     */
    uint64_t mapUserPages(::task::Task& target, uint64_t address, uint64_t size,
                          uint64_t protection) {
        task::Task* task = &target;

        // 页对齐、用户范围及权限组合先统一检查，后面的页循环只处理已经确认合法的区间。
        if (task == nullptr || task == task::getIdle() ||
            (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
            size > maximumMapSize || !validUserRange(address, size) ||
            (protection & ~3ULL) != 0) {
            return error(task::SyscallError::InvalidArgument);
        }

        if ((protection & 3) == 3) {
            return error(task::SyscallError::PermissionDenied);
        }

        task::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());

        if (!task->addressSpace().rootPhysicalAddress()) {
            return error(task::SyscallError::NotFound);
        }

        // 先试算新映射是否需要锁页及额度是否够，失败时不消耗原有锁页记录。
        ::task::AddressSpace::Residency residency =
            task->addressSpace().residency();

        if (task->addressSpace().lockFuture() &&
            (!residency.set(address, size, true) ||
             residency.bytes > task->memlockLimit())) {
            return error(task::SyscallError::OutOfMemory);
        }

        uint64_t flags = pageUser;

        if ((protection & 1) != 0) {
            flags |= pageWritable;
        }

        if ((protection & 2) == 0) {
            flags |= pageNoExecute;
        }

        // 累计本次已建立的长度，用于后续失败时仅回滚本次前缀。
        uint64_t mappedSize = 0;

        while (mappedSize < size) {
            const uint64_t physicalAddress = allocatePhysicalPage();

            if (physicalAddress == 0 ||
                !mapPageInAddressSpace(
                    task->addressSpace().rootPhysicalAddress(),
                    address + mappedSize, physicalAddress,
                    flags |
                        (residency.contains(address + mappedSize) ? pageLocked
                                                                  : 0))) {
                if (physicalAddress != 0) {
                    freePhysicalPage(physicalAddress);
                }

                // 先撤销映射并等所有相关 CPU
                // 清除旧翻译，再释放私有物理页，防止旧地址访问到已经重新分配的内存。
                while (mappedSize != 0) {
                    mappedSize -= pageSize;
                    const uint64_t physical = getPhysicalAddressInAddressSpace(
                        task->addressSpace().rootPhysicalAddress(),
                        address + mappedSize);
                    unmapPageInAddressSpace(
                        task->addressSpace().rootPhysicalAddress(),
                        address + mappedSize);
                    freePhysicalPage(physical & ~(pageSize - 1));
                }

                return error(task::SyscallError::OutOfMemory);
            }

            uint8_t* page =
                reinterpret_cast<uint8_t*>(directMapBase + physicalAddress);

            for (uint64_t index = 0; index < pageSize; ++index) {
                page[index] = 0;
            }

            mappedSize += pageSize;
        }

        task->addressSpace().residency() = residency;

        return address;
    }

    /*
     * 撤销用户页面映射。
     * 先为对象映射记录的拆分预留空间，再修改页表并更新对象记录，最后按归属归还普通物理页。
     */
    uint64_t unmapUserPages(::task::Task& target, uint64_t address,
                            uint64_t size, bool preserveLocks) {
        task::Task* task = &target;

        if (task == nullptr || task == task::getIdle() ||
            (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
            size > maximumMapSize || !validUserRange(address, size)) {
            return error(task::SyscallError::InvalidArgument);
        }

        task::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());

        if (!task->addressSpace().rootPhysicalAddress()) {
            return error(task::SyscallError::NotFound);
        }

        for (uint64_t offset = 0; offset < size; offset += pageSize) {
            uint64_t flags          = 0;
            const uint64_t physical = getPhysicalAddressInAddressSpace(
                task->addressSpace().rootPhysicalAddress(), address + offset,
                &flags);

            if (physical == 0 || (flags & pageUser) == 0 ||
                (flags & (1ULL << 7))) {
                return error(task::SyscallError::NotFound);
            }
        }

        ::task::AddressSpace::Residency residency =
            task->addressSpace().residency();

        // 普通解除映射同时撤销锁页记录；仅为替换页面的内部操作可以要求保留锁页约定。
        if (!preserveLocks && !residency.set(address, size, false)) {
            return error(task::SyscallError::OutOfMemory);
        }

        // 解除中间区间可能把对象记录切成两段，先取得所需节点再改页表，避免提交途中缺少记录空间。
        void* reservation       = nullptr;
        void* sharedReservation = nullptr;

        if (!sharedMemory().prepareUnmap(*task, address, size,
                                         sharedReservation)) {
            return error(task::SyscallError::OutOfMemory);
        }

        // 页对象记录预留失败时，取消已准备的共享映射拆分；此时尚未改页表，原映射和记录应完整保留。
        if (!maps().prepareUnmap(*task, address, size, reservation)) {
            sharedMemory().cancelUnmap(sharedReservation);

            return error(task::SyscallError::OutOfMemory);
        }

        const uint64_t root = task->addressSpace().rootPhysicalAddress();

        for (uint64_t offset = 0; offset < size;) {
            const uint64_t count = (size - offset) / pageSize < 64
                                       ? (size - offset) / pageSize
                                       : 64;
            uint64_t physical[64]{}, flags[64]{};

            for (uint64_t i = 0; i < count; ++i) {
                physical[i] = getPhysicalAddressInAddressSpace(
                    root, address + offset + i * pageSize, &flags[i]);
            }

            // 所有 CPU
            // 确认这批地址失效之前，物理页仍须保留；前面已在同一地址空间锁下检查全部普通页，提交阶段不再分配记录。
            unmapPagesInAddressSpace(root, address + offset, count * pageSize);

            for (uint64_t i = 0; i < count; ++i) {
                if (!(flags[i] & pageExternal)) {
                    freePhysicalPage(physical[i] & ~(pageSize - 1));
                }
            }

            offset += count * pageSize;
        }

        // 使用预留节点完成整段删除、边界缩短或中间拆分，不能等页表已经删除后才申请拆分记录。
        task->addressSpace().residency() = residency;
        maps().completeUnmap(*task, address, size, reservation);
        sharedMemory().releaseMapping(*task, address, size, sharedReservation);

        return 0;
    }

    /*
     * 调整用户地址区间权限。
     * 检查对象权限上限和范围有效性后，再修改页表并使相关 CPU 的旧地址缓存失效。
     */
    uint64_t protectUserPages(::task::Task& target, uint64_t address,
                              uint64_t size, uint64_t protection) {
        task::Task* task = &target;

        if (task == nullptr || task == task::getIdle() ||
            (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
            size > maximumMapSize || !validUserRange(address, size) ||
            (protection & ~3ULL) != 0 || (protection & 3) == 3) {
            return error(task::SyscallError::InvalidArgument);
        }

        task::PreemptionSpinLockGuard addressGuard(task->addressSpace().lock());

        if (!task->addressSpace().rootPhysicalAddress()) {
            return error(task::SyscallError::NotFound);
        }

        // 先检查页对象与共享对象原先授予的最高权限，任何一方不允许都不能通过通用改权限接口绕过。
        if (!sharedMemory().protectionAllowed(*task, address, size,
                                              protection) ||
            !maps().protectionAllowed(*task, address, size, protection)) {
            return error(task::SyscallError::PermissionDenied);
        }

        // 在真正修改前逐页核对映射存在和页面类型，尽量让可预见的失败发生在提交之前。
        for (uint64_t offset = 0; offset < size; offset += pageSize) {
            uint64_t existing = 0;

            if (!getPhysicalAddressInAddressSpace(
                    task->addressSpace().rootPhysicalAddress(),
                    address + offset, &existing) ||
                !(existing & pageUser) || (existing & (1ULL << 7))) {
                return error(task::SyscallError::NotFound);
            }
        }

        // 把公开的写入、执行权限转换为页表位；物理归属、访问和脏页状态由底层保留。
        uint64_t flags = pageUser;

        if ((protection & 1) != 0) {
            flags |= pageWritable;
        }

        if ((protection & 2) == 0) {
            flags |= pageNoExecute;
        }

        for (uint64_t offset = 0; offset < size;) {
            const uint64_t bytes =
                size - offset < 64 * pageSize ? size - offset : 64 * pageSize;

            if (!protectPagesInAddressSpace(
                    task->addressSpace().rootPhysicalAddress(),
                    address + offset, bytes, flags)) {
                return error(task::SyscallError::InvalidArgument);
            }

            offset += bytes;
        }

        return 0;
    }

}  // namespace mm
