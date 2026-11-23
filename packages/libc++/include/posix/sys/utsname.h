/*
 * 系统名称及版本。
 */

#ifndef _POSIX_SYS_UTSNAME_H
#define _POSIX_SYS_UTSNAME_H
#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* 保存系统名称、主机名、版本及机器架构字符串。 */
struct utsname {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
#ifdef _GNU_SOURCE
        char domainname[65];
#else
        char __domainname[65];
#endif
};

int uname(struct utsname *);

#ifdef __cplusplus
}
#endif

#endif

#endif
