#include <unistd.h>
#include <stdint.h>
#include "kernel/syscall.h"

pid_t clone(int (*fn)(void *), void *stack, int flags, void *arg) {
    MESSAGE msg;
    msg.num[0] = (uint64_t)fn;
    msg.num[1] = (uint64_t)stack;
    msg.num[2] = (uint64_t)flags;
    msg.num[3] = (uint64_t)arg;
    msgSend(SYS_TASK, SYS_TASK_CLONE, &msg);
    msgRecv(NULL, SYS_TASK, &msg);

    return msg.num[0];
}

pid_t fork() {
    MESSAGE msg;
    msg.num[0] = 0;
    msg.num[1] = 0;
    msg.num[2] = 0;
    msg.num[3] = 0;
    msgSend(SYS_TASK, SYS_TASK_CLONE, &msg);
    msgRecv(NULL, SYS_TASK, &msg);

    return msg.num[0];
}
