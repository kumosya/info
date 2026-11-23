/*
 * 进程与子进程 CPU 时间。
 */

#ifndef _POSIX_SYS_TIMES_H
#define _POSIX_SYS_TIMES_H
#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_clock_t
#include <bits/alltypes.h>

/* 保存进程及其已等待子进程消耗的用户态和内核态时钟数。 */
struct tms {
        clock_t tms_utime;
        clock_t tms_stime;
        clock_t tms_cutime;
        clock_t tms_cstime;
};

clock_t times(struct tms *);

#ifdef __cplusplus
}
#endif

#endif

#endif
