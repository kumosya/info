#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <kernel/syscall.h>

DIR *opendir(const char *name) {
    int fd = open(name, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        return NULL;
    }
    return (DIR *)(uintptr_t)fd;
}

struct dirent *readdir(DIR *dir) {
    if (!dir) {
        return NULL;
    }
    
    static struct dirent entry;
    int fd = (int)(uintptr_t)dir;
    
    MESSAGE msg;
    msg.num[0] = fd;
    msg.num[1] = (uint64_t)&entry;
    msgSend(SYS_FS, SYS_FS_READDIR, &msg);
    msgRecv(NULL, SYS_FS_READDIR, &msg);
    
    if (msg.num[0] == 0) {
        return NULL;
    }
    
    return &entry;
}

int closedir(DIR *dir) {
    if (!dir) {
        return -1;
    }
    int fd = (int)(uintptr_t)dir;
    return close(fd);
}

void rewinddir(DIR *dir) {
    if (!dir) {
        return;
    }
    int fd = (int)(uintptr_t)dir;
    lseek(fd, 0, SEEK_SET);
}

int dirfd(DIR *dir) {
    if (!dir) {
        return -1;
    }
    return (int)(uintptr_t)dir;
}
