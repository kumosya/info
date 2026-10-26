#define _GNU_SOURCE 1
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>
#include "internal.h"

static int priority(unsigned operation, int which, id_t who, int value) {
    int result = 0;
    const int saved = errno;
    int error = __libc_os_priority(operation, (unsigned)which, who, value, &result);
    if (error) { errno = error; return -1; }
    errno = saved;
    return operation == 1 ? 0 : result;
}
int nice(int increment) {
    int result = 0;
    const int saved = errno;
    const int error = __libc_os_priority(2, PRIO_PROCESS, 0, increment, &result);
    if (error) { errno = error == EACCES ? EPERM : error; return -1; }
    errno = saved;
    return result;
}
int getpriority(int which, id_t who) { return priority(0, which, who, 0); }
int setpriority(int which, id_t who, int value) { return priority(1, which, who, value); }
int prlimit(pid_t pid, int resource, const struct rlimit* input, struct rlimit* output) {
    if (pid < 0 || resource < 0 || resource >= RLIMIT_NLIMITS) { errno = EINVAL; return -1; }
    if ((input && !__libc_os_accessible(input, sizeof(*input), 0)) ||
        (output && !__libc_os_accessible(output, sizeof(*output), 1))) { errno = EFAULT; return -1; }
    uint64_t soft = input ? input->rlim_cur : 0, hard = input ? input->rlim_max : 0;
    const int error = __libc_os_resource_limit(pid, resource, input != 0, &soft, &hard);
    if (error) { errno = error; return -1; }
    if (output) { output->rlim_cur = soft; output->rlim_max = hard; }
    return 0;
}
int getrlimit(int resource, struct rlimit* output) {
    if (!output) { errno = EFAULT; return -1; }
    return prlimit(0, resource, 0, output);
}
int setrlimit(int resource, const struct rlimit* input) {
    if (!input) { errno = EFAULT; return -1; }
    return prlimit(0, resource, input, 0);
}
