#include <unistd.h>
#include "kernel/syscall.h"
#include <stdlib.h>

int execve(const char *filename, char *const argv[], char *const envp[]) {
    int ret;
    __asm__ __volatile__(
        "syscall		\n"
        : "=a"(ret)
        : "a"(SYS_TASK_EXECVE)
        : "memory");

    return ret;
}

int execv(const char *filename, char *const argv[]) {
    return execve(filename, argv, NULL);
}
