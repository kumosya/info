#include <stdlib.h>
#include <unistd.h>

#include <kernel/syscall.h>

void _exit(int status) {
    MESSAGE msg;
    msg.num[0]  = status;
    msgSend(SYS_TASK, SYS_TASK_EXIT, &msg);
}

void exit(int status) {
    _exit(status);
    return;
}