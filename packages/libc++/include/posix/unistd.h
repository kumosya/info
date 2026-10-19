#ifndef _POSIX_UNISTD_H
#define _POSIX_UNISTD_H


#include <stddef.h>
#include <fcntl.h>
#include <sys/types.h>

#define _POSIX_VERSION 202405L
#define _POSIX2_VERSION 202405L
#define _POSIX_C_SOURCE 202405L

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define _SC_VERSION 1
#define _SC_2_VERSION 2
#define _SC_OPEN_MAX 3
#define _SC_PAGESIZE 4
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_CLK_TCK 5
#define _SC_NPROCESSORS_CONF 6
#define _SC_NPROCESSORS_ONLN 7

#define _PC_LINK_MAX 1
#define _PC_MAX_CANON 2
#define _PC_MAX_INPUT 3
#define _PC_NAME_MAX 4
#define _PC_PATH_MAX 5
#define _PC_PIPE_BUF 6
#define _PC_CHOWN_RESTRICTED 7
#define _PC_NO_TRUNC 8
#define _PC_VDISABLE 9

#define _CS_PATH 1

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifdef __cplusplus
extern "C" {
#endif
extern char **environ;
extern char *optarg;
extern int optind, opterr, optopt;
ssize_t read(int fileDescriptor, void* buffer, size_t count);
ssize_t write(int fileDescriptor, const void* buffer, size_t count);
int close(int fileDescriptor);
int pipe(int fileDescriptors[2]);
int pipe2(int fileDescriptors[2], int flags);
int dup(int fileDescriptor);
int dup2(int fileDescriptor, int newFileDescriptor);
int dup3(int fileDescriptor, int newFileDescriptor, int flags);
off_t lseek(int fileDescriptor, off_t offset, int whence);
ssize_t pread(int fileDescriptor, void* buffer, size_t count, off_t offset);
ssize_t pwrite(int fileDescriptor, const void* buffer, size_t count,
               off_t offset);
int chdir(const char* path);
char* getcwd(char* buffer, size_t size);
int access(const char* path, int mode);
int unlink(const char* path);
int link(const char* existing, const char* new_path);
int linkat(int old_directory, const char* existing, int new_directory,
           const char* new_path, int flags);
int symlink(const char* target, const char* link_path);
ssize_t readlink(const char* path, char* buffer, size_t size);
int chown(const char* path, uid_t owner, gid_t group);
int fchown(int file_descriptor, uid_t owner, gid_t group);
int lchown(const char* path, uid_t owner, gid_t group);
int rmdir(const char* path);
int truncate(const char* path, off_t length);
int ftruncate(int fileDescriptor, off_t length);
int fsync(int fileDescriptor);
int fdatasync(int fileDescriptor);
int isatty(int fileDescriptor);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t microseconds);
pid_t getpid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t id);
int setgid(gid_t id);
pid_t getppid(void);
pid_t getpgrp(void);
pid_t getsid(pid_t taskId);
pid_t setsid(void);
long sysconf(int name);
long pathconf(const char* path, int name);
size_t confstr(int name, char* buffer, size_t length);
pid_t fork(void);
int execve(const char* path, char* const arguments[],
           char* const environment[]);
void _exit(int status) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
pid_t _Fork(void);
unsigned alarm(unsigned);
char *crypt(const char *, const char *);
void encrypt(char [64], int);
int execl(const char *, const char *, ...);
int execle(const char *, const char *, ...);
int execlp(const char *, const char *, ...);
int execv(const char *, char *const []);
int execvp(const char *, char *const []);
int faccessat(int, const char *, int, int);
int fchdir(int);
int fchownat(int, const char *, uid_t, gid_t, int);
int fexecve(int, char *const [], char *const []);
long fpathconf(int, int);
int getentropy(void *, size_t);
int getgroups(int, gid_t []);
long gethostid(void);
int gethostname(char *, size_t);
char *getlogin(void);
int getlogin_r(char *, size_t);
int getopt(int, char * const [], const char *);
pid_t getpgid(pid_t);
int getresgid(gid_t *__restrict, gid_t *__restrict, gid_t *__restrict);
int getresuid(uid_t *__restrict, uid_t *__restrict, uid_t *__restrict);
int lockf(int, int, off_t);
int nice(int);
int pause(void);
int posix_close(int, int);
ssize_t readlinkat(int, const char *__restrict, char *__restrict, size_t);
int setegid(gid_t);
int seteuid(uid_t);
int setpgid(pid_t, pid_t);
int setregid(gid_t, gid_t);
int setresgid(gid_t, gid_t, gid_t);
int setresuid(uid_t, uid_t, uid_t);
int setreuid(uid_t, uid_t);
void swab(const void *__restrict, void *__restrict, ssize_t);
int symlinkat(const char *, int, const char *);
void sync(void);
pid_t tcgetpgrp(int);
int tcsetpgrp(int, pid_t);
char *ttyname(int);
int ttyname_r(int, char *, size_t);
int unlinkat(int, const char *, int);
#ifdef __cplusplus
}
#endif

#endif
