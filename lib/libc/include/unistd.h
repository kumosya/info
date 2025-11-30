/* Public domain.  */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/types.h>

/* Types */
typedef unsigned int alarm_t;

/* utimbuf structure */
struct utimbuf {
    time_t actime;  /* access time */
    time_t modtime; /* modification time */
};

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* File descriptor macros */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Seek whence values */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Function declarations */
int _exit(int status);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);
off_t lseek(int fd, off_t offset, int whence);

/* File operations */
int unlink(const char *path);
int rename(const char *oldpath, const char *newpath);
int link(const char *oldpath, const char *newpath);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);

/* Directory operations */
int mkdir(const char *path, mode_t mode);
int rmdir(const char *path);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);

/* Process control */
pid_t getpid(void);
pid_t getppid(void);
pid_t fork(void);
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
int execve(const char *filename, char *const argv[], char *const envp[]);

/* User and group */
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

/* Time related */
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t useconds);
unsigned int alarm(unsigned int seconds);

/* File status */
int access(const char *path, int mode);
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);
int chown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
int utime(const char *filename, const struct utimbuf *times);

/* Memory management */
void *sbrk(intptr_t increment);
void *brk(void *addr);

/* Terminal */
int isatty(int fd);
char *ttyname(int fd);

/* Miscellaneous */
int sysconf(int name);
char *getlogin(void);

/* Access modes for access() */
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

/* Wait options for waitpid() */
#define WNOHANG  1
#define WUNTRACED 2

/* Wait status macros */
#define WIFEXITED(s)  (((s) & 0xFF) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFSIGNALED(s) (((s) & 0xFF) != 0 && ((s) & 0xFF) != 0x7F)
#define WTERMSIG(s) ((s) & 0x7F)
#define WIFSTOPPED(s) (((s) & 0xFF) == 0x7F)
#define WSTOPSIG(s) (((s) >> 8) & 0xFF)

#ifdef __cplusplus
}
#endif

#endif /* _UNISTD_H */