#pragma once

#include <stdint.h>

#include "kernel/cpu.h"

namespace kernel {

class SpinLock {
   public:
    void init() { locked_ = 0; }

    void lock() {
        while (__atomic_test_and_set(&locked_, __ATOMIC_ACQUIRE)) {
            asm volatile("pause");
        }
    }

    void unlock() { __atomic_clear(&locked_, __ATOMIC_RELEASE); }

    bool tryLock() {
        return !__atomic_test_and_set(&locked_, __ATOMIC_ACQUIRE);
    }

   private:
    uint8_t locked_ = 0;
};

class InterruptSpinLockGuard {
   public:
    explicit InterruptSpinLockGuard(SpinLock& lock)
        : lock_(lock), interruptFlags_(interrupt::disable()) {
        lock_.lock();
    }

    ~InterruptSpinLockGuard() {
        lock_.unlock();
        interrupt::restore(interruptFlags_);
    }

    InterruptSpinLockGuard(const InterruptSpinLockGuard&)            = delete;
    InterruptSpinLockGuard& operator=(const InterruptSpinLockGuard&) = delete;

   private:
    SpinLock& lock_;
    uint64_t interruptFlags_;
};

class PreemptionSpinLockGuard {
   public:
    explicit PreemptionSpinLockGuard(SpinLock& lock) : lock_(lock) {
        cpu::disablePreemption();
        lock_.lock();
    }

    ~PreemptionSpinLockGuard() {
        lock_.unlock();
        cpu::enablePreemption();
    }

    PreemptionSpinLockGuard(const PreemptionSpinLockGuard&)            = delete;
    PreemptionSpinLockGuard& operator=(const PreemptionSpinLockGuard&) = delete;

   private:
    SpinLock& lock_;
};

}  // namespace kernel
