/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 * atomic.h - Atomic operations implementation
 *
 */

#ifndef _KERNEL_ATOMIC_H_
#define _KERNEL_ATOMIC_H_

#include <cstdint>

namespace kernel {

enum class MemoryOrder : int {
    Relaxed        = __ATOMIC_RELAXED,
    Acquire        = __ATOMIC_ACQUIRE,
    Release        = __ATOMIC_RELEASE,
    AcquireRelease = __ATOMIC_ACQ_REL,
    Sequential     = __ATOMIC_SEQ_CST
};

template <typename Type>
class Atomic {
   public:
    constexpr Atomic() : value_{} {}
    constexpr explicit Atomic(Type value) : value_(value) {}

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
        __atomic_exchange(&value_, &value, &previous, static_cast<int>(order));
        return previous;
    }

    bool compareExchange(Type& expected, Type desired,
                         MemoryOrder success = MemoryOrder::Sequential,
                         MemoryOrder failure = MemoryOrder::Sequential) {
        return __atomic_compare_exchange_n(&value_, &expected, desired, false,
                                           static_cast<int>(success),
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

}  // namespace kernel

#endif  // _KERNEL_ATOMIC_H_
