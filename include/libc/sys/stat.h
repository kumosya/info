/* Public domain.  */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>
#include <time.h>

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* File type macros */
#define S_IFMT   0170000  /* File type mask */
#define S_IFSOCK 0140000  /* Socket */
#define S_IFLNK  0120000  /* Symbolic link */
#define S_IFREG  0100000  /* Regular file */
#define S_IFBLK  0060000  /* Block device */
#define S_IFDIR  0040000  /* Directory */
#define S_IFCHR  0020000  /* Character device */
#define S_IFIFO  0010000  /* FIFO */

/* Access permission macros */
#define S_ISUID  0004000  /* Set user ID on execution */
#define S_ISGID  0002000  /* Set group ID on execution */
#define S_ISVTX  0001000  /* Sticky bit */
#define S_IRWXU  0000700  /* User read, write, execute */
#define S_IRUSR  0000400  /* User read */
#define S_IWUSR  0000200  /* User write */
#define S_IXUSR  0000100  /* User execute */
#define S_IRWXG  0000070  /* Group read, write, execute */
#define S_IRGRP  0000040  /* Group read */
#define S_IWGRP  0000020  /* Group write */
#define S_IXGRP  0000010  /* Group execute */
#define S_IRWXO  0000007  /* Others read, write, execute */
#define S_IROTH  0000004  /* Others read */
#define S_IWOTH  0000002  /* Others write */
#define S_IXOTH  0000001  /* Others execute */

/* stat structure */
struct stat {
    dev_t     st_dev;     /* Device ID */
    ino_t     st_ino;     /* Inode number */
    mode_t    st_mode;    /* File mode */
    nlink_t   st_nlink;   /* Number of hard links */
    uid_t     st_uid;     /* User ID of owner */
    gid_t     st_gid;     /* Group ID of owner */
    dev_t     st_rdev;    /* Device ID (if special file) */
    off_t     st_size;    /* Total size, in bytes */
    blksize_t st_blksize; /* Block size for filesystem I/O */
    blkcnt_t  st_blocks;  /* Number of 512B blocks allocated */
    time_t    st_atime;   /* Time of last access */
    time_t    st_mtime;   /* Time of last modification */
    time_t    st_ctime;   /* Time of last status change */
};

/* Function declarations */
int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int mkdir(const char *path, mode_t mode);
int mkfifo(const char *path, mode_t mode);
int mknod(const char *path, mode_t mode, dev_t dev);
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);

/* File type checking macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_STAT_H */