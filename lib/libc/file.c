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

int chdir(const char *path) {
    MESSAGE msg;
    strcpy(msg.s.str, path);
    msgSend(SYS_FS, SYS_FS_CHDIR, &msg);
    msgRecv(NULL, SYS_FS_CHDIR, &msg);
    return (int)msg.num[0];
}

char *getcwd(char *buf, size_t size) {
    MESSAGE msg;
    msg.num[0] = (uint64_t)buf;
    msg.num[1] = size;
    msgSend(SYS_FS, SYS_FS_GETCWD, &msg);
    msgRecv(NULL, SYS_FS_GETCWD, &msg);
    return (char *)msg.num[0];
}

off_t lseek(int file, off_t offset, int whence) {
    MESSAGE msg;
    msg.num[0]  = file;
    msg.num[1]  = offset;
    msg.num[2]  = whence;
    msgSend(SYS_FS, SYS_FS_LSEEK, &msg);
    msgRecv(NULL, SYS_FS_LSEEK, &msg);
    return msg.num[0];
}

int mkdir(const char *path, mode_t mode) {
    MESSAGE msg;
    strcpy(msg.s.str, path);
    msgSend(SYS_FS, SYS_FS_MKDIR, &msg);
    msgRecv(NULL, SYS_FS_MKDIR, &msg);
    return (int)msg.num[0];
}

int rmdir(const char *path) {
    MESSAGE msg;
    strcpy(msg.s.str, path);
    msgSend(SYS_FS, SYS_FS_RMDIR, &msg);
    msgRecv(NULL, SYS_FS_RMDIR, &msg);
    return (int)msg.num[0];
}

int unlink(const char *path) {
    MESSAGE msg;
    strcpy(msg.s.str, path);
    msgSend(SYS_FS, SYS_FS_UNLINK, &msg);
    msgRecv(NULL, SYS_FS_UNLINK, &msg);
    return (int)msg.num[0];
}
