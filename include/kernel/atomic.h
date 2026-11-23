
/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 * atomic.h - Atomic operations implementation
 *
 */

#ifndef _KERNEL_ATOMIC_H_
#define _KERNEL_ATOMIC_H_

#include <cstdint>

enum class MemoryOrder : int {
    Relaxed        = __ATOMIC_RELAXED,
    Acquire        = __ATOMIC_ACQUIRE,
    Release        = __ATOMIC_RELEASE,
    AcquireRelease = __ATOMIC_ACQ_REL,
    Sequential     = __ATOMIC_SEQ_CST
};

/* 封装原子读写和读改写操作，使多 CPU 共享的单个值可以安全更新。 */
template <typename Type>
class Atomic {
        static_assert(__is_trivially_copyable(Type), "原子值必须可按字节复制");
        static_assert(sizeof(Type) <= 8 && (sizeof(Type) & (sizeof(Type) - 1)) == 0,
                      "内核原子值只支持 1、2、4、8 字节，不能依赖 libatomic");
    public:
        constexpr Atomic() : value_{} {}
        constexpr explicit Atomic(Type value) : value_(value) {}
        Atomic(const Atomic&) = delete;
        Atomic& operator=(const Atomic&) = delete;

        Type load(MemoryOrder order = MemoryOrder::Sequential) const {
            Type value;
            __atomic_load(&value_, &value, static_cast<int>(order));
            return value;
        }

        void store(Type value, MemoryOrder order = MemoryOrder::Sequential) {
            __atomic_store(&value_, &value, static_cast<int>(order));
        }

        Type exchange(Type value, MemoryOrder order = MemoryOrder::Sequential) {
            Type previous;
            __atomic_exchange(&value_, &value, &previous,
                              static_cast<int>(order));
            return previous;
        }

        bool compareExchange(Type& expected, Type desired,
                             MemoryOrder order = MemoryOrder::Sequential) {
            const MemoryOrder failure = order == MemoryOrder::Release ? MemoryOrder::Relaxed :
                order == MemoryOrder::AcquireRelease ? MemoryOrder::Acquire : order;
            return compareExchange(expected, desired, order, failure);
        }

        bool compareExchange(Type& expected, Type desired,
                             MemoryOrder success, MemoryOrder failure) {
            return __atomic_compare_exchange(&value_, &expected, &desired,
                                               false, static_cast<int>(success),
                                               static_cast<int>(failure));
        }

        Type fetchAdd(Type value, MemoryOrder order = MemoryOrder::Sequential) {
            return __atomic_fetch_add(&value_, value, static_cast<int>(order));
        }

        Type fetchSub(Type value, MemoryOrder order = MemoryOrder::Sequential) {
            return __atomic_fetch_sub(&value_, value, static_cast<int>(order));
        }

    private:
        alignas(sizeof(Type)) Type value_;
};

#endif  // _KERNEL_ATOMIC_H_
