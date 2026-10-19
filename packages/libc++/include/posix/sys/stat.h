#ifndef _POSIX_SYS_STAT_H
#define _POSIX_SYS_STAT_H


#include <time.h>

#include <sys/types.h>

#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFBLK 0060000
#define S_IFCHR 0020000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFSOCK 0140000
#define S_ISFIFO(mode) (((mode)&S_IFMT)==S_IFIFO)
#define S_ISLNK(mode) (((mode)&S_IFMT)==S_IFLNK)
#define S_ISSOCK(mode) (((mode)&S_IFMT)==S_IFSOCK)
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#define S_IRWXU 00700
#define S_IRWXG 00070
#define S_IRWXO 00007
#define S_ISBLK(mode) (((mode) & S_IFMT) == S_IFBLK)
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
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
int utimensat(int directory, const char* path, const struct timespec times[2],
              int flags);
#ifdef __cplusplus
}
#endif

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int fchmodat(int, const char *, mode_t, int);
int futimens(int, const struct timespec [2]);
int lstat(const char *__restrict, struct stat *__restrict);
int mkdirat(int, const char *, mode_t);
int mkfifo(const char *, mode_t);
int mkfifoat(int, const char *, mode_t);
int mknod(const char *, mode_t, dev_t);
int mknodat(int, const char *, mode_t, dev_t);
mode_t umask(mode_t);
#ifdef __cplusplus
}
#endif

#endif
