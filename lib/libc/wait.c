#include <sys/wait.h>
#include "kernel/syscall.h"
#include <stdio.h>

pid_t wait(int *status) {
    MESSAGE msg;
    msg.num[0] = -1;
    msgSend(SYS_TASK, SYS_TASK_WAIT, &msg);
    msgRecv(NULL, SYS_TASK, &msg);
    printf("wait: pid %d, status %d\n", msg.num[0], msg.num[1]);
    if (status) {
        *status = (int)msg.num[1];
    }
    return msg.num[0];
}

pid_t waitpid(pid_t pid, int *status, int options) {
    MESSAGE msg;
    msg.num[0] = pid;
    msg.num[1] = options;
    msgSend(SYS_TASK, SYS_TASK_WAITPID, &msg);
    msgRecv(NULL, SYS_TASK, &msg);
    if (status) {
        *status = (int)msg.num[1];
    }
    return msg.num[0];
}
