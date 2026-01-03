/* Public domain.  */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* System specific types */
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned long size_t;
typedef signed long ssize_t;
typedef long off_t;
typedef long loff_t;
typedef unsigned int mode_t;
typedef int dev_t;
typedef int ino_t;
typedef int nlink_t;
typedef int blksize_t;
typedef int blkcnt_t;
typedef int time_t;
typedef int clock_t;
typedef unsigned int useconds_t;
typedef int suseconds_t;

/* Socket types */
typedef int socklen_t;
typedef int sa_family_t;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TYPES_H */