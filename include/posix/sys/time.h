#ifndef _POSIX_SYS_TIME_H
#define _POSIX_SYS_TIME_H

#include <sys/types.h>

#ifndef __DEFINED_struct_timeval
struct timeval {
    time_t tv_sec;
    long tv_usec;
};
#define __DEFINED_struct_timeval
#endif

#ifdef __cplusplus
extern "C" {
#endif
int gettimeofday(struct timeval* value, void* timezone);
#ifdef __cplusplus
}
#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_struct_timeval

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int utimes(const char*, const struct timeval[2]);
#ifdef __cplusplus
}
#endif

#endif
