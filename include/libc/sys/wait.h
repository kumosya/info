/* Public domain.  */
#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* Wait status macros */
#define WNOHANG 1    /* 不阻塞，立即返回 */
#define WUNTRACED 2  /* 返回已停止的子进程 */
#define WCONTINUED 4 /* 返回已继续的子进程 */

/* Macros for interpreting status in wait() */
#define WIFEXITED(status) (((status) & 0xFF) == 0)   /* 进程正常退出 */
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF) /* 获取退出状态 */
#define WIFSIGNALED(status) \
    (((status) & 0xFF) != 0 && ((status) & 0xFF00) == 0) /* 进程被信号终止 */
#define WTERMSIG(status) ((status) & 0x7F)               /* 获取终止信号 */
#define WIFSTOPPED(status) (((status) & 0xFF00) != 0)    /* 进程已停止 */
#define WSTOPSIG(status) (((status) >> 8) & 0x7F)        /* 获取停止信号 */
#define WIFCONTINUED(status) ((status) == 0xFFFF)        /* 进程已继续 */

/* Function declarations */
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait3(int *status, int options, struct rusage *rusage);
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage);

/* rusage 结构的前向声明 */
struct rusage;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_WAIT_H */