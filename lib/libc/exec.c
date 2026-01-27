#include <unistd.h>
#include "kernel/syscall.h"
#include <stdlib.h>

int execve(const char *filename, char *const argv[], char *const envp[]) {
    MESSAGE msg;
    msg.num[0]  = (uint64_t)filename;
    msg.num[1]  = (uint64_t)argv;
    msg.num[2]  = (uint64_t)envp;
    msgSend(SYS_TASK, SYS_TASK_EXECVE, &msg);
}

int execv(const char *filename, char *const argv[]) {
    return execve(filename, argv, NULL);
}
