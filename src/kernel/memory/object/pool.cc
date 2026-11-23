/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/pool.cc
 * 在固定存储中分配等大小槽位，并检查重复释放。
 */

#include "kernel/mm.h"

namespace mm {

    /*
     * 用调用者提供的存储建立定长对象池。
     * 检查对象大小和容量后，用位图记录哪些槽位已经分配；对象池不另行拥有这段存储。
     */
    bool FixedPool::init(void* storage, size_t bytes, size_t size) {
        if (!storage || (reinterpret_cast<uint64_t>(storage) & 15) || !size ||
            size > SIZE_MAX - 15) {
            return false;
        }

        // 每个槽按 16
        // 字节向上对齐，再按整槽计算容量，尾部不足一个槽的字节不使用。
        const size_t stride = (size + 15) & ~15ULL, capacity = bytes / stride;

        if (!capacity || capacity > 4096 || stride > UINT32_MAX) {
            return false;
        }

        storage_  = storage;
        stride_   = stride;
        capacity_ = free_ = capacity;
        first_            = 0;
        lock_.init();

        // 位图负责判断槽位是否正在使用，初始时所有位清零。
        for (uint64_t& word : used_) {
            word = 0;
        }

        // 把空闲槽自身的头几个字节连成索引链，最后一个用 UINT32_MAX 表示结束。
        for (uint32_t i = 0; i < capacity_; ++i) {
            *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(storage_) +
                                         i * stride_) =
                i + 1 < capacity_ ? i + 1 : UINT32_MAX;
        }

        return true;
    }

    /*
     * 从空闲索引链取出槽位，并用位图标记占用后返回槽地址。
     * 没有空槽时返回空指针。
     */
    void* FixedPool::allocate() {
        ::task::InterruptSpinLockGuard guard(lock_);

        if (!storage_ || first_ == UINT32_MAX) {
            return nullptr;
        }

        // 从空闲链表头取一个槽，同时更新链头、空闲数量和位图；这些状态必须在同一把锁内修改。
        const uint32_t index = first_;
        void* object = static_cast<uint8_t*>(storage_) + index * stride_;
        first_       = *static_cast<uint32_t*>(object);
        --free_;
        used_[index / 64] |= 1ULL << (index % 64);

        return object;
    }

    /*
     * 核对指针范围、槽位对齐和占用状态，再清除对应位。
     * 拒绝外来指针以及重复释放。
     */
    bool FixedPool::release(void* object) {
        ::task::InterruptSpinLockGuard guard(lock_);
        const uint64_t offset = reinterpret_cast<uint64_t>(object) -
                                reinterpret_cast<uint64_t>(storage_);

        // 先检查指针落在存储内且恰好位于槽起点，防止把对象中间的地址当成完整对象归还。
        if (!storage_ || offset % stride_ || offset / stride_ >= capacity_) {
            return false;
        }

        const uint32_t index = offset / stride_;
        const uint64_t bit   = 1ULL << (index % 64);

        // 对应位没有置位表示该槽不在使用中，拒绝第二次释放同一个槽。
        if (!(used_[index / 64] & bit)) {
            return false;
        }

        // 标记空闲后把槽插回空闲链头，之后分配可以立即复用它。
        used_[index / 64] &= ~bit;
        *static_cast<uint32_t*>(object) = first_;
        first_                          = index;
        ++free_;

        return true;
    }

    /*
     * 在锁内读取剩余槽位数，避免与并发分配、释放的计数修改冲突。
     */
    size_t FixedPool::available() {
        ::task::InterruptSpinLockGuard guard(lock_);

        return free_;
    }

}  // namespace mm
