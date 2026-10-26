#ifndef _POSIX_SYS_WAIT_H
#define _POSIX_SYS_WAIT_H


#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WSTOPPED WUNTRACED
#define WCONTINUED 8

#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WTERMSIG(status) ((status) & 0x7f)
#define WCOREDUMP(status) (((status) & 0x80) != 0)
#define WIFSIGNALED(status) (WTERMSIG(status) != 0 && WTERMSIG(status) != 0x7f)
#define WSTOPSIG(status) WEXITSTATUS(status)
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#define WIFCONTINUED(status) ((status) == 0xffff)

#ifdef __cplusplus
extern "C" {
#endif
pid_t wait(int* status);
pid_t waitpid(pid_t taskId, int* status, int options);
#ifdef __cplusplus
}
#endif

/* Additional POSIX types. */
#include <signal.h>
typedef enum { P_ALL, P_PID, P_PGID } idtype_t;
#define WEXITED 4
#define WNOWAIT 0x01000000

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int waitid(idtype_t, id_t, siginfo_t *, int);
#ifdef __cplusplus
}
#endif

#endif
