#ifndef _POSIX_DIRENT_H
#define _POSIX_DIRENT_H

#include <sys/types.h>

#include <stdint.h>

#define DT_UNKNOWN 0
#define DT_REG 8
#define DT_DIR 4

struct dirent {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
};

typedef struct DirectoryStream DIR;

#ifdef __cplusplus
extern "C" {
#endif
DIR* opendir(const char* path);
struct dirent* readdir(DIR* directory);
int closedir(DIR* directory);
void rewinddir(DIR* directory);
void seekdir(DIR* directory, long location);
long telldir(DIR* directory);
int dirfd(DIR* directory);
#ifdef __cplusplus
}
#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_DIR

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int alphasort(const struct dirent **, const struct dirent **);
DIR *fdopendir(int);
ssize_t posix_getdents(int, void *, size_t, int);
int readdir_r(DIR *__restrict, struct dirent *__restrict, struct dirent **__restrict);
int scandir(const char *, struct dirent ***, int (*)(const struct dirent *), int (*)(const struct dirent **, const struct dirent **));
#ifdef __cplusplus
}
#endif

#endif
