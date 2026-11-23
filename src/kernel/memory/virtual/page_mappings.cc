/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/virtual/page_mappings.cc
 * 登记页对象映射的引用、权限和映像代号。
 */

#include <new>

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"

namespace mm {

    static Maps mappingRegistry;

    /*
     * 返回页对象映射登记表。
     * 页表记录地址翻译，这张表另外记录对象引用、映像代号和权限上限。
     */
    Maps& maps() { return mappingRegistry; }

    /*
     * 从固定记录池取一个映射节点。
     * 没有空闲记录时返回空指针。
     */
    Map* Maps::allocate() {
        void* value = pool.allocate();

        return value ? new (value) Map{} : nullptr;
    }

    /*
     * 初始化映射记录池和保护锁，供后续页对象映射使用。
     */
    bool Maps::init() {
        lock.init();
        mappings = nullptr;

        return pool.init(storage, sizeof(storage), sizeof(Map));
    }

    /*
     * 检查指定地址的对象映射是否允许写入核心转储，避免读取不适合导出的页面。
     */
    bool Maps::dumpAllowed(::task::Task& target, uint64_t address) {
        ::task::PreemptionSpinLockGuard guard(lock);

        for (const Map* r = mappings; r; r = r->next) {
            if (r->target == &target && r->pagerManaged &&
                address >= r->address && address - r->address < r->size) {
                return true;
            }
        }

        return false;
    }

    /*
     * 把页对象映射进目标任务。
     * 检查对象范围、地址和权限后建立页表，再发布映射记录并保留对象引用。
     */
    bool Maps::map(::task::Task& target, PageObject& object, uint64_t address,
                   uint64_t size, uint64_t offset, uint64_t protection,
                   uint64_t maximumProtection) {
        // 先检查目标地址和对象内偏移，并要求当前权限不超出句柄允许上限，后续才能安全按页循环。
        if (!size || ((address | size | offset) & 4095) || address < 0x10000 ||
            address + size <= address ||
            address + size > 0x0000800000000000ULL || offset > object.size() ||
            size > object.size() - offset || protection > 2 ||
            maximumProtection > 3 || (protection & ~maximumProtection)) {
            return false;
        }

        ::task::PreemptionSpinLockGuard space(target.addressSpace().lock());

        if (!target.addressSpace().rootPhysicalAddress()) {
            return false;
        }

        ::task::PreemptionSpinLockGuard guard(lock);

        // 登记表也必须没有重叠；即使某些页暂未驻留，也不能用新记录覆盖同一地址的旧对象关系。
        for (Map* r = mappings; r; r = r->next) {
            if (r->target == &target && r->overlaps(address, size)) {
                return false;
            }
        }

        // 在修改页表之前先取得登记节点，避免映射已生效才发现无处记录对象引用。
        Map* record = allocate();

        if (!record) {
            return false;
        }

        const uint64_t root = target.addressSpace().rootPhysicalAddress();
        uint64_t flags      = pageUser | pageExternal |
                         ((protection & 1) ? pageWritable : 0) |
                         ((protection & 2) ? 0 : pageNoExecute);
        ::task::AddressSpace::Residency residency =
            target.addressSpace().residency();
        const bool pager =
            task::getCurrent() && task::getCurrent() != &target &&
            task::getCurrent()->serviceRole() == os::ServiceRole::Memory;

        // 普通任务开启未来锁页时，新区间也计入额度；先在副本中计算，失败时不改原锁页表。
        if (!pager && target.addressSpace().lockFuture() &&
            (!residency.set(address, size, true) ||
             residency.bytes > target.memlockLimit())) {
            pool.release(record);

            return false;
        }

        // 按页建立映射并记录完成长度，后面若失败，就只撤销本次成功安装的前缀。
        uint64_t mapped = 0;

        while (mapped < size &&
               mm::mapPageInAddressSpace(
                   root, address + mapped, object.physical() + offset + mapped,
                   flags | (residency.contains(address + mapped) ? pageLocked
                                                                 : 0))) {
            mapped += 4096;
        }

        // 部分成功不向调用者发布，倒序撤销已安装页面并归还预留节点。
        if (mapped != size) {
            while (mapped) {
                mapped -= 4096;
                mm::unmapPageInAddressSpace(root, address + mapped);
            }

            pool.release(record);

            return false;
        }

        target.addressSpace().residency() = residency;

        // 页面全部安装成功后再增加长期对象引用并挂入记录表，句柄关闭后映射仍能保留页面。
        object.retain();
        *record = {&target, &object,           address, size,
                   offset,  maximumProtection, mappings};
        // 映射来源由正在执行的内存服务身份确认，不能相信用户自行提供的来源标志。
        record->pagerManaged =
            task::getCurrent() && task::getCurrent() != &target &&
            task::getCurrent()->serviceRole() == os::ServiceRole::Memory;
        mappings = record;

        return true;
    }

    /*
     * 查找目标当前映像中游标之后的下一段对象映射，返回地址和长度。
     */
    bool Maps::nextManagedMapping(::task::Task& target, uint64_t cursor,
                                  uint64_t& address, uint64_t& size) {
        ::task::PreemptionSpinLockGuard guard(lock);
        const Map* selected = nullptr;

        for (const Map* r = mappings; r; r = r->next) {
            if (r->target == &target && r->pagerManaged &&
                r->address >= cursor &&
                (!selected || r->address < selected->address)) {
                selected = r;
            }
        }

        if (!selected) {
            return false;
        }

        address = selected->address;
        size    = selected->size;

        return true;
    }

    /*
     * 预留解除映射可能需要的拆分节点。
     * 先准备记录空间，再允许调用者修改页表。
     */
    bool Maps::prepareUnmap(::task::Task& target, uint64_t address,
                            uint64_t size, void*& reservation) {
        reservation = nullptr;
        ::task::PreemptionSpinLockGuard guard(lock);

        for (Map* r = mappings; r; r = r->next) {
            if (r->target == &target && address > r->address &&
                address + size < r->address + r->size) {
                reservation = allocate();

                return reservation != nullptr;
            }
        }

        return true;
    }

    /*
     * 在页表解除后更新登记记录。
     * 完整覆盖就删除，部分覆盖就缩短或拆分，并相应调整对象引用。
     */
    void Maps::completeUnmap(::task::Task& target, uint64_t address,
                             uint64_t size, void* reserved) {
        ::task::PreemptionSpinLockGuard guard(lock);
        Map* spare         = static_cast<Map*>(reserved);
        const uint64_t end = address + size;
        Map** link         = &mappings;

        while (*link) {
            Map* r = *link;

            if (r->target != &target || !r->overlaps(address, size)) {
                link = &r->next;
                continue;
            }

            const uint64_t oldEnd = r->address + r->size;

            if (address <= r->address && end >= oldEnd) {
                *link = r->next;
                r->object->release();
                pool.release(r);
                continue;
            }

            if (address > r->address && end < oldEnd) {
                // 这个拆分节点在移除页表项之前就由 prepareUnmap 预留好了。
                *spare         = *r;
                spare->address = end;
                spare->size    = oldEnd - end;
                spare->offset += end - r->address;
                spare->object->retain();
                spare->next = r->next;
                r->next     = spare;
                spare       = nullptr;
                r->size     = address - r->address;
            } else if (address <= r->address) {
                // 删除映射前缀时，虚拟起点和对象内偏移必须一起前移，否则后续访问会指向错误的物理字节。
                r->offset += end - r->address;
                r->address = end;
                r->size    = oldEnd - end;
            } else {
                r->size = address - r->address;
            }

            link = &r->next;
        }

        if (spare) {
            pool.release(spare);
        }
    }

    /*
     * 释放任务的页对象映射记录。
     * 根据记录的映像代号识别有效映射，避免旧记录误操作 exec 后的新地址空间。
     */
    void Maps::releaseTask(::task::Task& target) {
        ::task::PreemptionSpinLockGuard guard(lock);
        Map** link = &mappings;

        while (*link) {
            Map* r = *link;

            if (r->target != &target) {
                link = &r->next;
                continue;
            }

            *link = r->next;
            r->object->release();
            pool.release(r);
        }
    }

    /*
     * 为 fork 复制对象映射登记并保留对象引用。
     * 失败时清理目标已增加的记录。
     */
    bool Maps::cloneTask(::task::Task& source, ::task::Task& target) {
        ::task::PreemptionSpinLockGuard guard(lock);
        uint32_t required = 0;

        for (Map* r = mappings; r; r = r->next) {
            if (r->target == &source) {
                ++required;
            }
        }

        if (pool.available() < required) {
            return false;
        }

        Map* copies = nullptr;

        for (Map* r = mappings; r; r = r->next) {
            if (r->target == &source) {
                Map* copy    = allocate();
                *copy        = *r;
                copy->target = &target;
                copy->object->retain();
                copy->next = copies;
                copies     = copy;
            }
        }

        while (copies) {
            Map* next    = copies->next;
            copies->next = mappings;
            mappings     = copies;
            copies       = next;
        }

        return true;
    }

    /*
     * 验证整段权限调整没有超过原对象映射的权限上限。
     */
    bool Maps::protectionAllowed(::task::Task& target, uint64_t address,
                                 uint64_t size, uint64_t protection) {
        ::task::PreemptionSpinLockGuard guard(lock);

        for (Map* r = mappings; r; r = r->next) {
            if (r->target == &target && r->overlaps(address, size) &&
                (((protection & 1) && (!(r->protection & 1) || r->cow)) ||
                 ((protection & 2) && !(r->protection & 2)))) {
                return false;
            }
        }

        return true;
    }

    /*
     * 把指定对象映射标记为写时复制，后续写访问必须先取得自己的私有页面。
     */
    bool Maps::markCopyOnWrite(::task::Task& target, uint64_t address,
                               uint64_t size) {
        ::task::PreemptionSpinLockGuard guard(lock);
        Map* record = nullptr;

        for (Map* r = mappings; r; r = r->next) {
            if (r->target == &target && r->address == address &&
                r->size == size) {
                record = r;
                break;
            }
        }

        if (!record) {
            return false;
        }

        const uint64_t root = target.addressSpace().rootPhysicalAddress();
        uint64_t flags      = 0;

        if (!root ||
            !mm::getPhysicalAddressInAddressSpace(root, address, &flags)) {
            return false;
        }

        for (uint64_t offset = 0; offset < size; offset += 4096) {
            if (!mm::protectPageInAddressSpace(root, address + offset,
                                               flags & ~pageWritable)) {
                return false;
            }
        }

        record->cow = true;

        return true;
    }

}  // namespace mm
