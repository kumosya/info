#pragma once

#include <cstdint>

#include "kernel/cpu.h"

namespace kernel::task {

class SpinLock {
   public:
    constexpr SpinLock()                 = default;
    SpinLock(const SpinLock&)            = delete;
    SpinLock& operator=(const SpinLock&) = delete;

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
        : lock_(lock), interruptFlags_(cpu::disableInterrupts()) {
        lock_.lock();
    }

    ~InterruptSpinLockGuard() {
        lock_.unlock();
        cpu::restoreInterrupts(interruptFlags_);
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

class Thread;

class WaitQueue {
   public:
    void init();
    void wait();
    bool wakeOne();
    uint32_t wakeAll();

   private:
    SpinLock lock_;
    Thread* head_ = nullptr;
    Thread* tail_ = nullptr;
};

class Mutex {
   public:
    void init();
    void lock();
    bool tryLock();
    void unlock();

   private:
    SpinLock lock_;
    Thread* owner_       = nullptr;
    Thread* waiters_     = nullptr;
    Thread* waitersTail_ = nullptr;
};

class Semaphore {
   public:
    void init(uint32_t count);
    void wait();
    bool tryWait();
    void signal();

   private:
    SpinLock lock_;
    uint32_t count_      = 0;
    Thread* waiters_     = nullptr;
    Thread* waitersTail_ = nullptr;
};

class Once {
   public:
    using Initializer = void (*)(void* argument);

    void init();
    void run(Initializer initializer, void* argument = nullptr);

   private:
    SpinLock lock_;
    uint8_t state_       = 0;
    Thread* waiters_     = nullptr;
    Thread* waitersTail_ = nullptr;
};

void runSynchronizationSelfTest();

int waitOnUserAddress(volatile uint32_t* address, uint32_t expected);
uint32_t wakeUserAddress(volatile uint32_t* address, uint32_t count);

}  // namespace kernel::task
