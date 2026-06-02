#include <sys/wait.h>
#include "kernel/syscall.h"

pid_t wait(int *status) {
    int ret;
    __asm__ __volatile__(
        "syscall		\n"
        : "=a"(ret)
        : "a"(SYS_TASK_WAIT)
        : "memory");

    return ret;
}

pid_t waitpid(pid_t pid, int *status, int options) {
    int ret;
    __asm__ __volatile__(
        "syscall		\n"
        : "=a"(ret)
        : "a"(SYS_TASK_WAITPID)
        : "memory");
    return ret;
}
