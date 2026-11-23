#include <errno.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "internal.h"

#define NANOSECONDS_PER_SECOND 1000000000ULL
extern int __libc_cpu_clock_read(clockid_t clock, uint64_t* value);

static int validTimespec(const struct timespec* value) {
    return value != NULL && value->tv_sec >= 0 && value->tv_nsec >= 0 &&
           value->tv_nsec < (long)NANOSECONDS_PER_SECOND;
}

time_t time(time_t* result) {
    time_t value = (time_t)(__libc_os_get_wall_clock_time() / NANOSECONDS_PER_SECOND);
    if (result != NULL) *result = value;
    return value;
}

int clock_gettime(int clockId, struct timespec* value) {
    uint64_t nanoseconds;
    if (value == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (clockId == CLOCK_MONOTONIC) nanoseconds = __libc_os_get_monotonic_time();
    else if (clockId == CLOCK_REALTIME) nanoseconds = __libc_os_get_wall_clock_time();
    else {
        int error = __libc_cpu_clock_read(clockId, &nanoseconds);
        if (error) { errno = error; return -1; }
    }
    value->tv_sec = (time_t)(nanoseconds / NANOSECONDS_PER_SECOND);
    value->tv_nsec = (long)(nanoseconds % NANOSECONDS_PER_SECOND);
    return 0;
}

int clock_getres(clockid_t clockId, struct timespec* value) {
    uint64_t resolution;
    if (clockId != CLOCK_MONOTONIC && clockId != CLOCK_REALTIME) {
        uint64_t unused;
        int error = __libc_cpu_clock_read(clockId, &unused);
        if (error) { errno = error; return -1; }
    }
    if (value == NULL) return 0;
    /* CPU 记账由 1 ms 调度时钟采样，不能把 TSC 的纳秒分辨率冒充记账精度。 */
    resolution = clockId == CLOCK_REALTIME || clockId == CLOCK_MONOTONIC ?
        __libc_os_get_clock_resolution() : 1000000ULL;
    value->tv_sec = (time_t)(resolution / NANOSECONDS_PER_SECOND);
    value->tv_nsec = (long)(resolution % NANOSECONDS_PER_SECOND);
    return 0;
}

/* 权限由内核按当前进程凭据检查，不能依靠 libc 的 uid 判断保护系统时钟。 */
int clock_settime(clockid_t clock, const struct timespec* value) {
    if (clock != CLOCK_REALTIME) { errno = EINVAL; return -1; }
    if (!value || !__libc_os_accessible(value, sizeof(*value), 0)) { errno = EFAULT; return -1; }
    if (!validTimespec(value) || (uint64_t)value->tv_sec > INT64_MAX / NANOSECONDS_PER_SECOND) {
        errno = EINVAL;
        return -1;
    }
    uint64_t nanoseconds = (uint64_t)value->tv_sec * NANOSECONDS_PER_SECOND + (uint64_t)value->tv_nsec;
    if (nanoseconds > INT64_MAX) { errno = EINVAL; return -1; }
    int error = __libc_os_set_wall_clock(nanoseconds);
    if (error) { errno = error; return -1; }
    return 0;
}

int nanosleep(const struct timespec* duration, struct timespec* remaining) {
    uint64_t milliseconds;
    if (!validTimespec(duration)) {
        errno = EINVAL;
        return -1;
    }
    milliseconds = (uint64_t)duration->tv_sec * 1000ULL +
                   ((uint64_t)duration->tv_nsec + 999999ULL) / 1000000ULL;
    __libc_os_sleep(milliseconds);
    if (remaining != NULL) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    return 0;
}

int clock_nanosleep(clockid_t clockId, int flags,
                    const struct timespec* duration,
                    struct timespec* remaining) {
    struct timespec delay;
    struct timespec now;
    if (clockId != CLOCK_MONOTONIC && clockId != CLOCK_REALTIME) return EINVAL;
    if (!validTimespec(duration) || (flags & ~TIMER_ABSTIME) != 0) return EINVAL;
    if ((flags & TIMER_ABSTIME) == 0) {
        return nanosleep(duration, remaining) == 0 ? 0 : errno;
    }
    if (clock_gettime(clockId, &now) != 0) return errno;
    if (duration->tv_sec < now.tv_sec ||
        (duration->tv_sec == now.tv_sec && duration->tv_nsec <= now.tv_nsec)) {
        return 0;
    }
    delay.tv_sec = duration->tv_sec - now.tv_sec;
    delay.tv_nsec = duration->tv_nsec - now.tv_nsec;
    if (delay.tv_nsec < 0) {
        --delay.tv_sec;
        delay.tv_nsec += NANOSECONDS_PER_SECOND;
    }
    return nanosleep(&delay, remaining) == 0 ? 0 : errno;
}

unsigned int sleep(unsigned int seconds) {
    struct timespec duration = {(time_t)seconds, 0};
    return nanosleep(&duration, NULL) == 0 ? 0 : seconds;
}

int usleep(useconds_t microseconds) {
    struct timespec duration;
    duration.tv_sec = microseconds / 1000000U;
    duration.tv_nsec = (long)(microseconds % 1000000U) * 1000L;
    return nanosleep(&duration, NULL);
}

int gettimeofday(struct timeval* value, void* timezone) {
    uint64_t nanoseconds;
    (void)timezone;
    if (value == NULL) {
        errno = EFAULT;
        return -1;
    }
    nanoseconds = __libc_os_get_wall_clock_time();
    value->tv_sec = (time_t)(nanoseconds / NANOSECONDS_PER_SECOND);
    value->tv_usec = (long)((nanoseconds % NANOSECONDS_PER_SECOND) / 1000ULL);
    return 0;
}
