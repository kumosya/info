#ifndef _DIRENT_H
#define _DIRENT_H

#if __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

struct dirent {
    char d_name[256];
    uint64_t d_ino;
    uint64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
};

typedef struct dirent DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);
void rewinddir(DIR *dir);
int dirfd(DIR *dir);

#if __cplusplus
}  // extern "C"
#endif

#endif
