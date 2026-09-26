#pragma once

#include <sys/types.h>

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#define CLOCK_MONOTONIC 1
#define CLOCK_REALTIME 0
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define TIMER_ABSTIME 1

#ifdef __cplusplus
extern "C" {
#endif
time_t time(time_t* result);
int clock_gettime(int clockId, struct timespec* value);
int clock_getres(clockid_t clockId, struct timespec* value);
int nanosleep(const struct timespec* duration, struct timespec* remaining);
int clock_nanosleep(clockid_t clockId, int flags,
                    const struct timespec* duration,
                    struct timespec* remaining);
#ifdef __cplusplus
}
#endif
