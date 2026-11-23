#define _GNU_SOURCE 1
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include "internal.h"

/* 负编号编码目标和时钟种类，正编号保留给公共 CLOCK_* 常量。 */
static int target_clock(uint64_t id, int thread, clockid_t* result)
{
    if (!result) return EFAULT;
    if (id > (uint64_t)(INT_MAX - 17) / 2) return EOVERFLOW;
    uint64_t unused;
    int error = __libc_os_cpu_time(id, thread, &unused);
    if (error) return error;
    *result = -(clockid_t)(id * 2 + 16 + thread);
    return 0;
}

int clock_getcpuclockid(pid_t pid, clockid_t* result)
{
    if (pid < 0) return ESRCH;
    return target_clock(pid, 0, result);
}

int pthread_getcpuclockid(pthread_t thread, clockid_t* result)
{
    if (!thread) return ESRCH;
    return target_clock(thread, 1, result);
}

/* clock_gettime/getres 共用解析，目标线程或进程退出后返回 ESRCH。 */
int __libc_cpu_clock_read(clockid_t clock, uint64_t* value)
{
    if (clock == CLOCK_PROCESS_CPUTIME_ID || clock == CLOCK_THREAD_CPUTIME_ID)
        return __libc_os_cpu_time(0, clock == CLOCK_THREAD_CPUTIME_ID, value);
    if (clock > -16) return EINVAL;
    uint64_t encoded = (uint64_t)(-(int64_t)clock) - 16;
    return __libc_os_cpu_time(encoded / 2, encoded & 1, value);
}

/* clock() 按 CLOCKS_PER_SEC 返回进程 CPU 用时，并检查返回类型容量。 */
clock_t clock(void)
{
    uint64_t nanoseconds;
    int error = __libc_os_cpu_time(0, 0, &nanoseconds);
    if (error) { errno = error; return (clock_t)-1; }
    uint64_t ticks = nanoseconds / (1000000000ULL / CLOCKS_PER_SEC);
    if (ticks > LONG_MAX) { errno = EOVERFLOW; return (clock_t)-1; }
    return (clock_t)ticks;
}
