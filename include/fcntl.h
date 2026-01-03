/* Public domain.  */
#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* File access modes */
#define O_RDONLY 0000
#define O_WRONLY 0001
#define O_RDWR 0002
#define O_ACCMODE 0003

/* File creation flags */
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000

/* Open flags */
#define O_SYNC 010000
#define O_DIRECTORY 020000
#define O_NOFOLLOW 040000
#define O_CLOEXEC 0200000

/* File descriptor flags */
#define FD_CLOEXEC 1

/* fcntl() commands */
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7

/* File locking structures */
struct flock {
    short l_type;   /* Type of lock: F_RDLCK, F_WRLCK, F_UNLCK */
    short l_whence; /* How to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END */
    off_t l_start;  /* Starting offset for lock */
    off_t l_len;    /* Number of bytes to lock */
    pid_t l_pid;    /* PID of process holding lock */
};

/* Lock types */
#define F_RDLCK 1 /* Read lock */
#define F_WRLCK 2 /* Write lock */
#define F_UNLCK 3 /* Unlock */

/* Function declarations */
int open(const char *path, int oflag, ...);
int creat(const char *path, mode_t mode);
int fcntl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif /* _FCNTL_H */