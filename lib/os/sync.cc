#include <os/sync.h>

namespace os {
void SpinLock::lock() {
    while (__atomic_test_and_set(&locked_, __ATOMIC_ACQUIRE)) {
        asm volatile("pause");
    }
}
bool SpinLock::tryLock() {
    return !__atomic_test_and_set(&locked_, __ATOMIC_ACQUIRE);
}
void SpinLock::unlock() { __atomic_clear(&locked_, __ATOMIC_RELEASE); }
}  // namespace os

namespace os {
SpinLock::Guard::Guard(SpinLock& lock) : lock_(lock) { lock_.lock(); }
SpinLock::Guard::~Guard() { lock_.unlock(); }
}  // namespace os

#include <os/syscall.h>
namespace os {
Mutex::Mutex() = default;
void Mutex::lock() {
    while (!tryLock()) waitAddress(&locked_, 1);
}
bool Mutex::tryLock() {
    uint32_t expected = 0;
    return __atomic_compare_exchange_n(&locked_, &expected, 1, false,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
void Mutex::unlock() {
    __atomic_store_n(&locked_, 0, __ATOMIC_RELEASE);
    wakeAddress(&locked_, 1);
}
Mutex::Guard::Guard(Mutex& mutex) : mutex_(mutex) { mutex_.lock(); }
Mutex::Guard::~Guard() { mutex_.unlock(); }
AddressWait::AddressWait(volatile uint32_t& address) : address_(address) {}
int AddressWait::wait(uint32_t expected) {
    return waitAddress(&address_, expected);
}
int AddressWait::wakeOne() { return wakeAddress(&address_, 1); }
int AddressWait::wakeAll() { return wakeAddress(&address_, UINT32_MAX); }
}  // namespace os
