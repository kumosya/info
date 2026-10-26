/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <signal.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int sigaltstack(const stack_t *restrict arg0, stack_t *restrict arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int sigqueue(pid_t arg0, int arg1, union sigval arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
