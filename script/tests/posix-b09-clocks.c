#define _GNU_SOURCE 1
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define CHECK(expression) do { if (!(expression)) { \
    printf("B09 FAIL line=%d errno=%d\n", __LINE__, errno); return __LINE__; } } while (0)

static uint64_t nanoseconds(struct timespec value)
{
    return (uint64_t)value.tv_sec * 1000000000ULL + value.tv_nsec;
}

int main(void)
{
    clockid_t process, thread;
    CHECK(clock_getcpuclockid(getpid(), &process) == 0);
    CHECK(pthread_getcpuclockid(pthread_self(), &thread) == 0);
    struct timespec first, second, resolution;
    CHECK(clock_gettime(process, &first) == 0);
    volatile uint64_t sum = 0;
    for (unsigned index = 0; index < 2000000; ++index) sum += index;
    CHECK(sum != 0 && clock_gettime(process, &second) == 0);
    CHECK(nanoseconds(second) >= nanoseconds(first));
    CHECK(clock_gettime(thread, &first) == 0);
    CHECK(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &second) == 0);
    CHECK(nanoseconds(second) >= nanoseconds(first));
    CHECK(clock_getres(thread, &resolution) == 0 && nanoseconds(resolution) > 0);
    CHECK(clock() != (clock_t)-1);
    CHECK(clock_getcpuclockid(-1, &process) == ESRCH);
    CHECK(pthread_getcpuclockid(0, &thread) == ESRCH);
    CHECK(clock_gettime(12345, &first) == -1 && errno == EINVAL);
    struct timespec wall, monotonic;
    CHECK(clock_gettime(CLOCK_REALTIME, &wall) == 0);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &monotonic) == 0);
    struct timespec adjusted = wall;
    adjusted.tv_sec += 30;
    CHECK(clock_settime(CLOCK_REALTIME, &adjusted) == 0);
    CHECK(clock_gettime(CLOCK_REALTIME, &first) == 0 && first.tv_sec >= adjusted.tv_sec);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &second) == 0 && nanoseconds(second) >= nanoseconds(monotonic));
    CHECK(clock_settime(CLOCK_REALTIME, &wall) == 0);
    CHECK(seteuid(1000) == 0);
    int denied = clock_settime(CLOCK_REALTIME, &adjusted);
    int denied_error = errno;
    CHECK(seteuid(0) == 0);
    CHECK(denied == -1 && denied_error == EPERM);
    return 0;
}
