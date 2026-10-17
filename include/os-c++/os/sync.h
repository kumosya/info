#ifndef _OS_SYNC_H
#define _OS_SYNC_H

#include <stdint.h>

namespace os {

class SpinLock {
   public:
    void lock() {
        while (__atomic_test_and_set(&locked_, __ATOMIC_ACQUIRE)) {
            asm volatile("pause");
        }
    }

    bool tryLock() {
        return !__atomic_test_and_set(&locked_, __ATOMIC_ACQUIRE);
    }

    void unlock() { __atomic_clear(&locked_, __ATOMIC_RELEASE); }

   private:
    uint8_t locked_ = 0;
};

class SpinLockGuard {
   public:
    explicit SpinLockGuard(SpinLock& lock) : lock_(lock) { lock_.lock(); }
    ~SpinLockGuard() { lock_.unlock(); }

    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;

   private:
    SpinLock& lock_;
};

}  // namespace os

#endif
