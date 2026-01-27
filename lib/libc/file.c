#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/syscall.h>

int open(const char *path, int flags, mode_t mode) {
    MESSAGE msg;
    strcpy(msg.s.str, path);
    msg.s.arg = flags;
    msgSend(SYS_FS, SYS_FS_OPEN, &msg);
    msgRecv(NULL, SYS_FS_OPEN, &msg);
    return (int)msg.num[0];
}

ssize_t read(int fd, void *buf, size_t count) {
    MESSAGE msg;
    msg.num[0]  = fd;
    msg.num[1]  = (uint64_t)buf;
    msg.num[2]  = count;
    msgSend(SYS_FS, SYS_FS_READ, &msg);
    msgRecv(NULL, SYS_FS_READ, &msg);
    return (ssize_t)msg.num[0];
}

ssize_t write(int fd, const void *buf, size_t count) {
    MESSAGE msg;
    msg.num[0]  = fd;
    msg.num[1]  = (uint64_t)buf;
    msg.num[2]  = count;
    msgSend(SYS_FS, SYS_FS_WRITE, &msg);
    msgRecv(NULL, SYS_FS_WRITE, &msg);
    return (ssize_t)msg.num[0];
}

int close(int fd) {
    MESSAGE msg;
    msg.num[0]  = fd;
    msgSend(SYS_FS, SYS_FS_CLOSE, &msg);
    msgRecv(NULL, SYS_FS_CLOSE, &msg);
    return (int)msg.num[0];
}

int dup(int oldfd) {
    MESSAGE msg;
    msg.num[0]  = oldfd;
    msgSend(SYS_FS, SYS_FS_DUP, &msg);
    msgRecv(NULL, SYS_FS_DUP, &msg);
    return (int)msg.num[0];
}

int dup2(int oldfd, int newfd) {
    MESSAGE msg;
    msg.num[0]  = oldfd;
    msg.num[1]  = newfd;
    msgSend(SYS_FS, SYS_FS_DUP2, &msg);
    msgRecv(NULL, SYS_FS_DUP2, &msg);
    return (int)msg.num[0];
}
