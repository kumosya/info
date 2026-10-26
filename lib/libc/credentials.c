#define _GNU_SOURCE 1
#include <errno.h>
#include <grp.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include "internal.h"

static int snapshot(OsCredentialInfo* info) {
    const int error = __libc_os_credentials(0, 0, 0, 0, NULL, info);
    if (error) { errno = error; return -1; } return 0;
}
static int change(unsigned operation, uint32_t first, uint32_t second, uint32_t third) {
    const int error = __libc_os_credentials(operation, first, second, third, NULL, NULL);
    if (error) { errno = error; return -1; } return 0;
}
uid_t getuid(void) { OsCredentialInfo info; return snapshot(&info) ? (uid_t)-1 : info.uid; }
uid_t geteuid(void) { OsCredentialInfo info; return snapshot(&info) ? (uid_t)-1 : info.effectiveUid; }
gid_t getgid(void) { OsCredentialInfo info; return snapshot(&info) ? (gid_t)-1 : info.gid; }
gid_t getegid(void) { OsCredentialInfo info; return snapshot(&info) ? (gid_t)-1 : info.effectiveGid; }
int setuid(uid_t id) { return change(1, id, 0, 0); }
int setgid(gid_t id) { return change(2, id, 0, 0); }
int setreuid(uid_t real, uid_t effective) { return change(3, real, effective, 0); }
int setregid(gid_t real, gid_t effective) { return change(4, real, effective, 0); }
int setresuid(uid_t real, uid_t effective, uid_t saved) { return change(5, real, effective, saved); }
int setresgid(gid_t real, gid_t effective, gid_t saved) { return change(6, real, effective, saved); }
int seteuid(uid_t id) { return change(7, id, 0, 0); }
int setegid(gid_t id) { return change(8, id, 0, 0); }
static int outputs(void* first, void* second, void* third) {
    if (!first || !second || !third || !__libc_os_accessible(first, 4, 1) ||
        !__libc_os_accessible(second, 4, 1) || !__libc_os_accessible(third, 4, 1)) { errno = EFAULT; return -1; }
    return 0;
}
int getresuid(uid_t* restrict real, uid_t* restrict effective, uid_t* restrict saved) {
    OsCredentialInfo info; if (outputs(real, effective, saved) || snapshot(&info)) return -1;
    *real = info.uid; *effective = info.effectiveUid; *saved = info.savedUid; return 0;
}
int getresgid(gid_t* restrict real, gid_t* restrict effective, gid_t* restrict saved) {
    OsCredentialInfo info; if (outputs(real, effective, saved) || snapshot(&info)) return -1;
    *real = info.gid; *effective = info.effectiveGid; *saved = info.savedGid; return 0;
}
int getgroups(int size, gid_t* groups) {
    if (size < 0) { errno = EINVAL; return -1; }
    OsCredentialInfo info; if (snapshot(&info)) return -1;
    if (!size) return (int)info.groupCount;
    if ((unsigned)size < info.groupCount) { errno = EINVAL; return -1; }
    if (info.groupCount && (!groups || !__libc_os_accessible(groups, info.groupCount * sizeof(*groups), 1))) { errno = EFAULT; return -1; }
    for (uint32_t i = 0; i < info.groupCount; ++i) groups[i] = info.groups[i];
    return (int)info.groupCount;
}
int setgroups(size_t size, const gid_t* groups) {
    if (size > 32) { errno = EINVAL; return -1; }
    if (size && (!groups || !__libc_os_accessible(groups, size * sizeof(*groups), 0))) { errno = EFAULT; return -1; }
    const int error = __libc_os_credentials(9, size, 0, 0, groups, NULL);
    if (error) { errno = error; return -1; } return 0;
}
