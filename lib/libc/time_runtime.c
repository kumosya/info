#include <errno.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "os/c_api.h"

#define NANOSECONDS_PER_SECOND 1000000000ULL

static int validTimespec(const struct timespec* value) {
    return value != NULL && value->tv_sec >= 0 && value->tv_nsec >= 0 &&
           value->tv_nsec < (long)NANOSECONDS_PER_SECOND;
}

time_t time(time_t* result) {
    time_t value = (time_t)(os_get_wall_clock_time() / NANOSECONDS_PER_SECOND);
    if (result != NULL) *result = value;
    return value;
}

int clock_gettime(int clockId, struct timespec* value) {
    uint64_t nanoseconds;
    if (value == NULL ||
        (clockId != CLOCK_MONOTONIC && clockId != CLOCK_REALTIME)) {
        errno = EINVAL;
        return -1;
    }
    nanoseconds = clockId == CLOCK_MONOTONIC ? os_get_monotonic_time()
                                             : os_get_wall_clock_time();
    value->tv_sec = (time_t)(nanoseconds / NANOSECONDS_PER_SECOND);
    value->tv_nsec = (long)(nanoseconds % NANOSECONDS_PER_SECOND);
    return 0;
}

int clock_getres(clockid_t clockId, struct timespec* value) {
    uint64_t resolution;
    if (clockId != CLOCK_MONOTONIC && clockId != CLOCK_REALTIME) {
        errno = EINVAL;
        return -1;
    }
    if (value == NULL) return 0;
    resolution = os_get_clock_resolution();
    value->tv_sec = (time_t)(resolution / NANOSECONDS_PER_SECOND);
    value->tv_nsec = (long)(resolution % NANOSECONDS_PER_SECOND);
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
    os_sleep(milliseconds);
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
    nanoseconds = os_get_wall_clock_time();
    value->tv_sec = (time_t)(nanoseconds / NANOSECONDS_PER_SECOND);
    value->tv_usec = (long)((nanoseconds % NANOSECONDS_PER_SECOND) / 1000ULL);
    return 0;
}
