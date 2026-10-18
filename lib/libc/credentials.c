#include <errno.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include "os_adapter.h"

static int currentTaskInfo(OsTaskInfo* info) {
    const long id = __libc_os_get_task_id();
    return id > 0 && __libc_os_query_task(id, info) == 0;
}

uid_t getuid(void) { OsTaskInfo info; return currentTaskInfo(&info) ? info.uid : 0; }
uid_t geteuid(void) { OsTaskInfo info; return currentTaskInfo(&info) ? info.effectiveUid : 0; }
gid_t getgid(void) { OsTaskInfo info; return currentTaskInfo(&info) ? info.gid : 0; }
gid_t getegid(void) { OsTaskInfo info; return currentTaskInfo(&info) ? info.effectiveGid : 0; }
int setuid(uid_t id) {
    OsTaskInfo info;
    if (!currentTaskInfo(&info)) { errno = ESRCH; return -1; }
    const long result = __libc_os_set_credentials(0, id, info.gid, 0);
    if (result < 0) { errno = result == -3 ? EPERM : EINVAL; return -1; }
    return 0;
}
int setgid(gid_t id) {
    OsTaskInfo info;
    if (!currentTaskInfo(&info)) { errno = ESRCH; return -1; }
    const long result = __libc_os_set_credentials(0, info.uid, id, 0);
    if (result < 0) { errno = result == -3 ? EPERM : EINVAL; return -1; }
    return 0;
}
