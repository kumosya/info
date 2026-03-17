#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "kernel/syscall.h"
#include "kernel/task.h"

extern "C" int putchar(int c) {
    task::Message msg;
    msg.dst_pid = 4;
    msg.type    = SYS_CHAR_PUTCHAR;
    msg.num[0]  = c;
    msg.Send();
    return c;
}

extern "C" int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    task::Message msg;
    int ret = vsprintf(msg.data, fmt, args);
    va_end(args);
    msg.dst_pid = 4;
    msg.type    = SYS_CHAR_PUTS;
    msg.Send();
    return ret;
}

extern "C" int getchar() {
    task::Message msg;
    msg.dst_pid = 4;
    msg.type    = SYS_CHAR_GETCHAR;
    msg.Send();
    msg.Recv();
    putchar(msg.num[0]);

    return msg.num[0];
}

extern "C" int open(const char *path, int flags, mode_t mode) {
    task::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_OPEN;
    strcpy(msg.s.str, path);
    msg.s.arg = flags;
    msg.Send();
    msg.Recv();
    return static_cast<int>(msg.num[0]);
}

extern "C" ssize_t read(int file, void *buf, std::uint64_t size) {
    task::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_READ;
    msg.num[0]  = file;
    msg.num[1]  = reinterpret_cast<std::uint64_t>(buf);
    msg.num[2]  = size;
    msg.Send();
    msg.Recv();
    return msg.num[0];
}

extern "C" off_t lseek(int file, off_t offset, int whence) {
    task::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_LSEEK;
    msg.num[0]  = file;
    msg.num[1]  = offset;
    msg.num[2]  = whence;
    msg.Send();
    msg.Recv();
    return msg.num[0];
}

extern "C" ssize_t write(int file, const void *buf, std::uint64_t size) {
    task::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_WRITE;
    msg.num[0]  = file;
    msg.num[1]  = reinterpret_cast<std::uint64_t>(buf);
    msg.num[2]  = size;
    msg.Send();
    msg.Recv();
    return msg.num[0];
}

extern "C" int close(int file) {
    task::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_CLOSE;
    msg.num[0]  = file;
    msg.Send();
    msg.Recv();
    return msg.num[0];
}

extern "C" int dup(int oldfd) {
    task::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_DUP;
    msg.num[0]  = oldfd;
    msg.Send();
    msg.Recv();
    return msg.num[0];
}

extern "C" int dup2(int oldfd, int newfd) {
    task::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_DUP2;
    msg.num[0]  = oldfd;
    msg.num[1]  = newfd;
    msg.Send();
    msg.Recv();
    return msg.num[0];
}
/*
DirEntry *readdir(const char *path, std::uint64_t index) {
    task::Message msg;
    msg.dst_pid = 3;
    msg.type    = SYS_FS_READDIR;
    strcpy(msg.s.str, path);
    msg.s.arg = index;
    msg.Send();
    msg.Recv();
    return reinterpret_cast<DirEntry *>(msg.num[0]);
}
*/