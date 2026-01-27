#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/syscall.h>
#include <dirent.h>

DIR *opendir(const char *name) {
    MESSAGE msg;
    strcpy(msg.s.str, name);
    msgSend(SYS_FS, SYS_FS_OPENDIR, &msg);
    msgRecv(NULL, SYS_FS_OPENDIR, &msg);
    return (DIR *)msg.num[0];
}
struct dirent *readdir(DIR *dir) {
    MESSAGE msg;
    msg.num[0]  = (uint64_t)dir;
    msgSend(SYS_FS, SYS_FS_READDIR, &msg);
    msgRecv(NULL, SYS_FS_READDIR, &msg);
    return (struct dirent *)msg.num[0];
}
int closedir(DIR *dir) {
    MESSAGE msg;
    msg.num[0]  = (uint64_t)dir;
    msgSend(SYS_FS, SYS_FS_CLOSEDIR, &msg);
    msgRecv(NULL, SYS_FS_CLOSEDIR, &msg);
    return (int)msg.num[0];
}
