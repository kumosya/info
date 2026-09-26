#pragma once

#include <sys/types.h>

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8

#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WTERMSIG(status) ((status) & 0x7f)
#define WIFSIGNALED(status) (WTERMSIG(status) != 0 && WTERMSIG(status) != 0x7f)
#define WSTOPSIG(status) WEXITSTATUS(status)
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#define WIFCONTINUED(status) ((status) == 0xffff)

#ifdef __cplusplus
extern "C" {
#endif
pid_t wait(int* status);
pid_t waitpid(pid_t processId, int* status, int options);
#ifdef __cplusplus
}
#endif
