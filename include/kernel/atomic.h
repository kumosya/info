#pragma once

#include <stdint.h>

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
        return __atomic_load_n(&value_, static_cast<int>(order));
    }

    void store(Type value, MemoryOrder order = MemoryOrder::Sequential) {
        __atomic_store_n(&value_, value, static_cast<int>(order));
    }

    Type exchange(Type value, MemoryOrder order = MemoryOrder::Sequential) {
        return __atomic_exchange_n(&value_, value, static_cast<int>(order));
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
