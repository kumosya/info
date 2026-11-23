/*
 * 锁、等待和同步原语。
 */

#pragma once

#include <cstdint>

#include "kernel/cpu.h"

namespace task {

    /* 使用原子状态忙等互斥，适合不能睡眠的短临界区。 */
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

    /* 先保存并关闭本地中断再取得自旋锁，离开作用域时解锁并恢复中断。 */
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

            InterruptSpinLockGuard(const InterruptSpinLockGuard&) = delete;
            InterruptSpinLockGuard& operator=(const InterruptSpinLockGuard&) =
                delete;

        private:
            SpinLock& lock_;
            uint64_t interruptFlags_;
    };

    /* 在限制抢占期间持有自旋锁，允许必须由中断完成的跨核协作。 */
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

            PreemptionSpinLockGuard(const PreemptionSpinLockGuard&) = delete;
            PreemptionSpinLockGuard& operator=(const PreemptionSpinLockGuard&) =
                delete;

        private:
            SpinLock& lock_;
    };

    class Thread;

    /* 登记等待某个条件的线程，并支持唤醒一个或全部等待者。 */
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

    /* 互斥访问一份资源，竞争失败的线程进入睡眠队列。 */
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

    /* 用计数管理可用名额，没有名额时让线程等待。 */
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

    /* 保证初始化只完成一次，并协调其他等待初始化结果的线程。 */
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

    int waitOnUserAddress(volatile uint32_t* address, uint32_t expected,
                          uint64_t deadline = 0);
    uint64_t readinessGeneration();
    int waitOnReadiness(uint64_t expected, uint64_t deadline, uint64_t signals);
    void notifyReadiness();
    uint32_t wakeUserAddress(volatile uint32_t* address, uint32_t count);
    void interruptUserAddressWait(Thread* thread);

}  // namespace task
