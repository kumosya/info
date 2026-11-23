/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/physical/buddy.cc
 * 将连续物理页按二次幂拆分与合并，记录节点、归属和引用。
 */

#include <new>

#include "kernel/page.h"

namespace mm {

    /*
     * 初始化物理页元数据、各节点空闲链表和锁。
     * 所有页面先标为保留，只有启动扫描确认可用的区间才会加入分配器。
     */
    void BuddyAlloc::init(PageFrame* frames, uint32_t count) {
        frames_ = frames;
        count_  = count;
        free_   = 0;
        lock_.init();

        for (uint16_t node = 0; node < maximumMemoryNodes; ++node) {
            for (uint32_t order = 0; order <= maximumBuddyOrder; ++order) {
                heads_[node][order] = noFrame;
            }
        }

        for (uint32_t i = 0; i < count; ++i) {
            new (&frames_[i]) PageFrame{};
            frames_[i].head = i;
        }
    }

    /*
     * 重写一个页块的元数据，让块内每一页都指向块头。
     * 大小、归属和引用以块头为准，避免把中间页误当成独立分配。
     */
    void BuddyAlloc::mark(uint32_t head, uint32_t order, uint16_t node,
                          FrameState state) {
        for (uint32_t i = 0; i < (1U << order); ++i) {
            PageFrame& frame = frames_[head + i];
            frame.reset();
            frame.head  = head;
            frame.node  = node;
            frame.state = FrameState::Interior;
        }

        frames_[head].state = state;
        frames_[head].order = order;
    }

    /*
     * 把空闲块头加入对应内存节点和大小的链表。
     * 调用者持有分配器锁，链表里只存块头。
     */
    void BuddyAlloc::insert(uint32_t head) {
        PageFrame& frame = frames_[head];
        uint32_t& first  = heads_[frame.node][frame.order];
        frame.previous   = noFrame;
        frame.next       = first;

        if (first != noFrame) {
            frames_[first].previous = head;
        }

        first = head;
    }

    /*
     * 从双向空闲链表摘下块头并清空链接。
     * 调用者持锁；清空链接可避免后续拆分或合并误用旧关系。
     */
    void BuddyAlloc::remove(uint32_t head) {
        PageFrame& frame = frames_[head];

        if (frame.previous == noFrame) {
            heads_[frame.node][frame.order] = frame.next;
        } else {
            frames_[frame.previous].next = frame.next;
        }

        if (frame.next != noFrame) {
            frames_[frame.next].previous = frame.previous;
        }

        frame.next = frame.previous = noFrame;
    }

    /*
     * 把尚未使用的物理区间加入空闲池。
     * 先确认整段仍为保留状态，再按地址对齐和剩余长度拆成若干个二次幂页块。
     */
    bool BuddyAlloc::addRange(uint32_t first, uint32_t count, uint16_t node) {
        if (!frames_ || !count || first >= count_ || count > count_ - first ||
            node >= maximumMemoryNodes) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(lock_);

        // 整段都确认仍是保留页之后才增加空闲计数，避免检查到一半失败却已改变分配状态。
        for (uint32_t i = first; i < first + count; ++i) {
            if (frames_[i].state != FrameState::Reserved) {
                return false;
            }
        }

        free_ += count;
        // 选择从 first
        // 开始、对齐且不超过剩余范围的最大块；对齐错误会使伙伴计算越过可用区间。
        while (count) {
            uint32_t order = 0;

            while (order < maximumBuddyOrder &&
                   (first & ((1U << (order + 1)) - 1)) == 0 &&
                   (1U << (order + 1)) <= count) {
                ++order;
            }

            // 为选定块写好块头和中间页信息，再挂入该节点对应大小的空闲链。
            mark(first, order, node, FrameState::Free);
            insert(first);
            first += 1U << order;
            count -= 1U << order;
        }

        return true;
    }

    /*
     * 申请包含 2 的 order 次方个页面的连续块。
     * 优先选择指定节点，把过大的空闲块逐级对半拆开，多余半块放回空闲链表；找不到时返回零。
     */
    uint64_t BuddyAlloc::allocate(uint32_t order, uint64_t owner,
                                  uint16_t preferred, bool strict,
                                  uint64_t limit) {
        if (!frames_ || order > maximumBuddyOrder ||
            (preferred != anyMemoryNode && preferred >= maximumMemoryNodes)) {
            return 0;
        }

        ::task::InterruptSpinLockGuard guard(lock_);

        // 优先遍历指定节点；严格节点分配不能退到其他节点，普通分配才继续寻找替代空间。
        for (uint16_t pass = 0; pass < maximumMemoryNodes; ++pass) {
            const uint16_t node = preferred == anyMemoryNode
                                      ? pass
                                      : (preferred + pass) % maximumMemoryNodes;

            if (strict && preferred != anyMemoryNode && pass) {
                break;
            }

            // 从所需大小开始逐级找更大的空块；找大块后才能通过对半拆分得到目标大小。
            for (uint32_t found = order; found <= maximumBuddyOrder; ++found) {
                uint32_t head = heads_[node][found];

                // 候选块必须避开零地址，并保证请求部分不越过设备或调用者指定的物理上限。
                while (head != noFrame &&
                       (head == 0 ||
                        (static_cast<uint64_t>(head) + (1ULL << order)) *
                                physicalPageSize >
                            limit)) {
                    head = frames_[head].next;
                }

                if (head == noFrame) {
                    continue;
                }

                remove(head);
                // 继续拆分较低地址的一半，把较高地址的一半放回空闲链；拆分本身不消耗页面，只有最终分配的部分扣减空闲计数。
                while (found > order) {
                    --found;
                    const uint32_t buddy = head + (1U << found);
                    mark(buddy, found, node, FrameState::Free);
                    insert(buddy);
                }

                // 拆分完成后才登记分配归属和初始引用，空闲计数只扣除真正交出的页面。
                mark(head, order, node, FrameState::Allocated);
                frames_[head].owner      = owner;
                frames_[head].references = 1;
                free_ -= 1ULL << order;

                return static_cast<uint64_t>(head) * physicalPageSize;
            }
        }

        return 0;
    }

    /*
     * 给已分配块增加引用。
     * 只接受块头，并检查所有者和引用上限，避免给已释放或不属于调用方的块续命。
     */
    bool BuddyAlloc::retain(uint64_t address, uint64_t owner) {
        if ((address & 4095) || address / 4096 >= count_) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(lock_);
        PageFrame& frame = frames_[address / 4096];

        if (frame.state != FrameState::Allocated || !frame.references ||
            frame.references == UINT32_MAX ||
            (owner != UINT64_MAX && frame.owner != owner)) {
            return false;
        }

        ++frame.references;

        return true;
    }

    /*
     * 归还一份物理块引用。
     * 计数归零后才尝试与同节点、同大小的空闲伙伴合并，最后把合并后的块放回链表。
     */
    bool BuddyAlloc::release(uint64_t address, uint32_t order, uint64_t owner) {
        if ((address & 4095) || address / 4096 >= count_ ||
            order > maximumBuddyOrder) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(lock_);
        uint32_t head    = address / 4096;
        PageFrame& frame = frames_[head];

        // 核对块头、大小、引用和所有者，避免错误地址或不匹配的阶数破坏空闲链表。
        if (frame.state != FrameState::Allocated || frame.order != order ||
            !frame.references ||
            (owner != UINT64_MAX && frame.owner != owner)) {
            return false;
        }

        // 还有其他使用者时只减引用，不改变页面内容，也不尝试与伙伴合并。
        if (--frame.references) {
            return true;
        }

        const uint16_t node = frame.node;
        free_ += 1ULL << order;

        while (order < maximumBuddyOrder) {
            // 异或对应大小的位得到伙伴块头；只有同节点、同大小且空闲的块才能合并，不能跨过保留区或仍在使用的内存。
            const uint32_t buddy = head ^ (1U << order);

            if (buddy >= count_ || frames_[buddy].state != FrameState::Free ||
                frames_[buddy].node != node || frames_[buddy].order != order) {
                break;
            }

            remove(buddy);
            // 合并块使用较低地址作为块头，再向上一级继续尝试合并。
            if (buddy < head) {
                head = buddy;
            }

            ++order;
        }

        // 得到最终合并范围后统一重写块内元数据，只把一个块头放回空闲链。
        mark(head, order, node, FrameState::Free);
        insert(head);

        return true;
    }

    /*
     * 给尚未归属任务的物理块登记所有者，或确认已有归属。
     * 已经属于其他任务的活块不能被直接改名。
     */
    bool BuddyAlloc::assignOwner(uint64_t address, uint64_t owner) {
        if ((address & 4095) || address / 4096 >= count_) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(lock_);
        PageFrame& frame = frames_[address / 4096];

        if (frame.state != FrameState::Allocated ||
            (frame.owner && frame.owner != owner)) {
            return false;
        }

        frame.owner = owner;

        return true;
    }

    /*
     * 由任意块内地址找到块头，返回整个块的归属、引用数和大小；不会把中间页算成另一块内存。
     */
    bool BuddyAlloc::query(uint64_t address, PhysicalPageInfo& info) {
        if (address / 4096 >= count_) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(lock_);
        const PageFrame& frame = frames_[frames_[address / 4096].head];
        info = {frame.owner, frame.references, frame.node, frame.order,
                frame.state};

        return true;
    }

    /*
     * 统计指定所有者占用的页数。
     * 只统计块头，并按块大小累计，避免重复计算块内页面。
     */
    uint64_t BuddyAlloc::pagesOwned(uint64_t owner) {
        ::task::InterruptSpinLockGuard guard(lock_);
        uint64_t total = 0;

        for (uint32_t i = 0; i < count_; ++i) {
            if (frames_[i].state == FrameState::Allocated &&
                frames_[i].owner == owner) {
                total += 1ULL << frames_[i].order;
            }
        }

        return total;
    }

    /*
     * 在分配器锁内读取当前空闲页数。
     */
    uint64_t BuddyAlloc::freePages() {
        ::task::InterruptSpinLockGuard guard(lock_);

        return free_;
    }

}  // namespace mm
