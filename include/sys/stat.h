#pragma once

#include <sys/types.h>
#include <time.h>

#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    nlink_t st_nlink;
    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};

#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

#define UTIME_NOW 1073741823L
#define UTIME_OMIT 1073741822L

#ifdef __cplusplus
extern "C" {
#endif
int stat(const char* path, struct stat* status);
int fstat(int fileDescriptor, struct stat* status);
int mkdir(const char* path, mode_t mode);
int chmod(const char* path, mode_t mode);
int fchmod(int fileDescriptor, mode_t mode);
int fstatat(int directory, const char* path, struct stat* status, int flags);
int utimensat(int directory, const char* path,
              const struct timespec times[2], int flags);
#ifdef __cplusplus
}
#endif
