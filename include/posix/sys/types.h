#ifndef _POSIX_SYS_TYPES_H
#define _POSIX_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int32_t pid_t;
typedef uint32_t mode_t;
typedef int64_t time_t;
typedef int32_t clockid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint64_t nlink_t;
typedef int64_t blksize_t;
typedef int64_t blkcnt_t;
typedef uint32_t useconds_t;

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_ssize_t
#define __DEFINED_off_t
#define __DEFINED_pid_t
#define __DEFINED_mode_t
#define __DEFINED_time_t
#define __DEFINED_clockid_t
#define __DEFINED_uid_t
#define __DEFINED_gid_t
#define __DEFINED_dev_t
#define __DEFINED_ino_t
#define __DEFINED_nlink_t
#define __DEFINED_blksize_t
#define __DEFINED_blkcnt_t
#define __DEFINED_useconds_t

/* Additional POSIX types. */
typedef unsigned int id_t;
typedef int key_t;
typedef long clock_t;
typedef long suseconds_t;
typedef unsigned long fsblkcnt_t;
typedef unsigned long fsfilcnt_t;
typedef void *timer_t;

#endif
