#pragma once
#include "internal.h"
static inline void libcWaitLock(volatile uint32_t* lock) {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(lock, &expected, 1U, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    while (__atomic_exchange_n(lock, 2U, __ATOMIC_ACQUIRE)) __libc_os_wait_address(lock, 2);
}
static inline void libcWaitUnlock(volatile uint32_t* lock) {
    if (__atomic_exchange_n(lock, 0U, __ATOMIC_RELEASE) == 2U) __libc_os_wake_address(lock, 1);
}
