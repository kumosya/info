/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/page_object.cc
 * 用对象引用管理连续物理页块的寿命。
 */

#include "kernel/mm.h"
#include "kernel/page.h"

namespace mm {

    /*
     * 申请属于指定任务的连续页块，并在对外使用前清零。
     * 申请失败时 physical_ 保持为零，由调用者检查对象是否有效。
     */
    PageObject::PageObject(uint32_t order, uint64_t owner, uint16_t node)
        : owner_(owner), order_(order) {
        if (order > 12) {
            return;
        }

        physical_ = allocatePhysicalBlock(order, owner, node);

        if (!physical_) {
            return;
        }

        clearPhysicalPages(physical_, size());
    }

    /*
     * 归还对象持有的最后一份物理页块引用。
     * 归还时使用创建时记录的大小和所有者。
     */
    PageObject::~PageObject() {
        if (physical_) {
            releasePhysicalBlock(physical_, order_, owner_);
        }
    }

    /*
     * 同时增加对象引用和物理页块引用。
     * 这样关闭句柄之后，仍被映射使用的页面可以继续存活。
     */
    void PageObject::retain() {
        // 每个映射同时持有对象引用和物理块引用，两者必须成对增减。
        retainPhysicalBlock(physical_, owner_);
        __atomic_add_fetch(&references_, 1U, __ATOMIC_RELAXED);
    }

    /*
     * 减少对象引用。
     * 最后一份引用负责析构并释放对象存储；其余引用只归还配对的页块引用，避免同一份引用被减两次。
     */
    void PageObject::release() {
        // 原子减计数并判断是否由本次调用负责最终销毁，多核同时归还也只能有一个最后释放者。
        const bool last =
            __atomic_sub_fetch(&references_, 1U, __ATOMIC_ACQ_REL) == 0;

        // 最后一份物理引用由析构函数归还；非最后引用走下面的单次归还，两个分支不能同时执行。
        if (last) {
            this->~PageObject();
            mm::freeHeap(this);
        } else if (physical_) {
            releasePhysicalBlock(physical_, order_, owner_);
        }
    }

}  // namespace mm
