#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "kernel/syscall.h"
#include "kernel/task.h"

extern "C" int putchar(int c) {
    task::ipc::Message msg;
    msg.dst_pid = 4;
    msg.type    = SYS_CHAR_PUTCHAR;
    msg.num[0]  = c;
    task::ipc::Send(&msg);
    return c;
}

extern "C" int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    task::ipc::Message msg;
    int ret = vsprintf(msg.data, fmt, args);
    va_end(args);
    msg.dst_pid = 4;
    msg.type    = SYS_CHAR_PUTS;
    task::ipc::Send(&msg);
    return ret;
}

extern "C" int getchar() {
    task::ipc::Message msg;
    msg.dst_pid = 4;
    msg.type    = SYS_CHAR_GETCHAR;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    putchar(msg.num[0]);

    return msg.num[0];
}

extern "C" int open(const char *path, int flags, mode_t mode) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_OPEN;
    strcpy(msg.s.str, path);
    msg.s.arg = flags;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return static_cast<int>(msg.num[0]);
}

extern "C" ssize_t read(int file, void *buf, std::uint64_t size) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_READ;
    msg.num[0]  = file;
    msg.num[1]  = reinterpret_cast<std::uint64_t>(buf);
    msg.num[2]  = size;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}

extern "C" off_t lseek(int file, off_t offset, int whence) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_LSEEK;
    msg.num[0]  = file;
    msg.num[1]  = offset;
    msg.num[2]  = whence;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}

extern "C" ssize_t write(int file, const void *buf, std::uint64_t size) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_WRITE;
    msg.num[0]  = file;
    msg.num[1]  = reinterpret_cast<std::uint64_t>(buf);
    msg.num[2]  = size;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}

extern "C" int close(int file) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_CLOSE;
    msg.num[0]  = file;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}

extern "C" int dup(int oldfd) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_DUP;
    msg.num[0]  = oldfd;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}

extern "C" int dup2(int oldfd, int newfd) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_DUP2;
    msg.num[0]  = oldfd;
    msg.num[1]  = newfd;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return msg.num[0];
}
/*
DirEntry *readdir(const char *path, std::uint64_t index) {
    task::ipc::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_READDIR;
    strcpy(msg.s.str, path);
    msg.s.arg = index;
    task::ipc::Send(&msg);
    task::ipc::Receive(&msg);
    return reinterpret_cast<DirEntry *>(msg.num[0]);
}
*/