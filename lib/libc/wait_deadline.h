#pragma once
#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "internal.h"

/* 将绝对期限转换为内核单调时钟期限。实时钟分段等待以跟随校时。 */
static inline int libcDeadlineWait(volatile uint32_t* address,
                                   uint32_t expected, clockid_t clock,
                                   const struct timespec* limit) {
    if ((clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC) || !limit ||
        limit->tv_nsec < 0 || limit->tv_nsec >= 1000000000L)
        return EINVAL;

    struct timespec now;
    if (clock_gettime(clock, &now)) return errno;
    if (limit->tv_sec < now.tv_sec ||
        (limit->tv_sec == now.tv_sec && limit->tv_nsec <= now.tv_nsec))
        return ETIMEDOUT;

    uint64_t seconds = (uint64_t)limit->tv_sec - (uint64_t)now.tv_sec;
    uint64_t duration;
    if (seconds > UINT64_MAX / 1000000000ULL)
        duration = UINT64_MAX;
    else {
        duration = seconds * 1000000000ULL;
        if (limit->tv_nsec >= now.tv_nsec) {
            uint64_t part = (uint64_t)(limit->tv_nsec - now.tv_nsec);
            duration =
                duration > UINT64_MAX - part ? UINT64_MAX : duration + part;
        } else
            duration -= (uint64_t)(now.tv_nsec - limit->tv_nsec);
    }
    if (clock == CLOCK_REALTIME && duration > 10000000ULL)
        duration = 10000000ULL;
    uint64_t monotonic = __libc_os_get_monotonic_time();
    uint64_t deadline  = duration >= UINT64_MAX - monotonic
                             ? UINT64_MAX - 1
                             : monotonic + duration;
    int result = __libc_os_wait_address_until(address, expected, deadline);
    if (result == -1 || result == -15) return EINVAL;
    if (result == -10) return EINTR;
    /* 分段到期后由上层重查对象和原始期限，不把内部片段当成用户超时。 */
    return 0;
}
