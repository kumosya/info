/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/slab.cc
 * 以物理页容纳小对象，管理空槽并回收全空页面。
 */

#include "kernel/mm.h"
#include "kernel/page.h"

namespace mm {

    constexpr uint64_t slabMagic = 0x534c414243414348ULL;

    // 一页中保存管理头和等间距对象槽；空槽自身存放下一个空槽指针，位图和使用数量分别用于合法性检查及整页回收。
    class alignas(16) ObjectCache::Slab {
            friend class ObjectCache;

        public:
            /*
             * 检查这一页对象存储是否还有空槽，供分配器选择可复用的页。
             */
            bool hasSpace() const { return first != nullptr; }

        private:
            uint64_t magic;
            ObjectCache* owner;
            Slab* previous;
            Slab* next;
            void* first;
            uint64_t used[4];
            uint32_t capacity, live;
    };

    /*
     * 记录对象大小并初始化链表和锁。
     * 一个缓存只处理一种大小的对象。
     */
    void ObjectCache::init(size_t size) {
        lock_.init();
        slabs_  = nullptr;
        empty_  = 0;
        stride_ = size && size <= 2048 ? (size + 15) & ~15ULL : 0;
    }

    /*
     * 从缓存链表摘下一页。
     * 调用者负责持锁，并在摘下后决定是否释放该页。
     */
    void ObjectCache::unlink(Slab* slab) {
        if (slab->previous) {
            slab->previous->next = slab->next;
        } else {
            slabs_ = slab->next;
        }

        if (slab->next) {
            slab->next->previous = slab->previous;
        }
    }

    /*
     * 先寻找已有空槽的页；都满了才申请新页。
     * 使用位图挑出一个对象位置并增加该页的使用计数。
     */
    void* ObjectCache::allocate() {
        if (!stride_) {
            return nullptr;
        }

        ::task::InterruptSpinLockGuard guard(lock_);
        Slab* slab = slabs_;

        // 先复用已有页面的空槽，只有所有页都装满了才申请新页。
        while (slab && !slab->hasSpace()) {
            slab = slab->next;
        }

        // 新页还没有对外使用；申请或映射失败时只需归还本次资源，不影响已有缓存页。
        if (!slab) {
            const uint64_t physical = allocatePhysicalBlock(0);

            if (!physical) {
                return nullptr;
            }

            slab = static_cast<Slab*>(mapKernelPages(physical, 0));

            if (!slab) {
                releasePhysicalBlock(physical, 0);

                return nullptr;
            }

            // 初始化页面的归属和链表关系，使后续释放可以从对象地址找到并验证所属缓存。
            slab->magic    = slabMagic;
            slab->owner    = this;
            slab->previous = nullptr;
            slab->next     = slabs_;

            if (slabs_) {
                slabs_->previous = slab;
            }

            slabs_         = slab;
            slab->capacity = (4096 - sizeof(Slab)) / stride_;
            slab->live     = 0;

            for (uint64_t& word : slab->used) {
                word = 0;
            }

            slab->first = nullptr;
            // 只有空闲槽可以当作链表节点；活对象的内容不能拿来存链接，是否占用由独立位图记录。
            for (uint32_t i = 0; i < slab->capacity; ++i) {
                uint8_t* object =
                    reinterpret_cast<uint8_t*>(slab + 1) + i * stride_;
                *reinterpret_cast<void**>(object) = slab->first;
                slab->first                       = object;
            }

            ++empty_;
        }

        // 从空闲链摘一个槽，再按其在页内的位置设置占用位；这以后槽里的字节归调用者使用。
        void* object         = slab->first;
        slab->first          = *static_cast<void**>(object);
        const uint32_t index = (static_cast<uint8_t*>(object) -
                                reinterpret_cast<uint8_t*>(slab + 1)) /
                               stride_;
        slab->used[index / 64] |= 1ULL << (index % 64);

        // 页面从全空变成有活对象，必须同步减少全空页面计数。
        if (!slab->live++) {
            --empty_;
        }

        return object;
    }

    /*
     * 确定对象所属的缓存页，检查它是否落在合法槽位上，再清除占用位。
     * 对象所在的页先保留，供后续申请复用。
     */
    bool ObjectCache::release(void* object) {
        if (!object || !stride_) {
            return false;
        }

        // 对象与管理头处于同一物理页，向下对齐页边界即可找到所属页面。
        Slab* slab = reinterpret_cast<Slab*>(
            reinterpret_cast<uint64_t>(object) & ~4095ULL);
        ::task::InterruptSpinLockGuard guard(lock_);

        if (slab->magic != slabMagic || slab->owner != this || !slab->live) {
            return false;
        }

        const uint64_t offset = static_cast<uint8_t*>(object) -
                                reinterpret_cast<uint8_t*>(slab + 1);

        // 必须命中完整槽位，不能接受管理头、页尾空隙或槽位中间的地址。
        if (offset % stride_ || offset / stride_ >= slab->capacity) {
            return false;
        }

        const uint32_t index = offset / stride_;
        const uint64_t bit   = 1ULL << (index % 64);

        if (!(slab->used[index / 64] & bit)) {
            return false;
        }

        // 先清占用位，再把对象自身连回空闲链；归还后原调用者不能再访问这段内容。
        slab->used[index / 64] &= ~bit;
        *static_cast<void**>(object) = slab->first;
        slab->first                  = object;
        // 保留一页全空存储供后续复用，多余空页则归还物理分配器；释放前先使管理头失效。
        if (--slab->live == 0) {
            if (empty_) {
                unlink(slab);
                slab->magic = 0;
                releasePhysicalBlock(physicalFromKernelPointer(slab), 0);
            } else {
                ++empty_;
            }
        }

        return true;
    }

    /*
     * 释放使用计数为零的缓存页。
     * 仍有活对象的页必须保留，返回本次归还的字节数。
     */
    uint64_t ObjectCache::shrink() {
        ::task::InterruptSpinLockGuard guard(lock_);
        uint64_t pages = 0;

        for (Slab* slab = slabs_; slab;) {
            // 当前页面可能马上释放，先保存下一页，释放后不能再从当前页读取
            // next。
            Slab* next = slab->next;

            // 只有活对象数为零的整页才可释放，部分空闲的页仍包含其他对象。
            if (!slab->live) {
                unlink(slab);
                slab->magic = 0;
                --empty_;
                releasePhysicalBlock(physicalFromKernelPointer(slab), 0);
                ++pages;
            }

            slab = next;
        }

        return pages;
    }

}  // namespace mm
