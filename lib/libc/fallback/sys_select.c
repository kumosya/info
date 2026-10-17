/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/select.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int pselect(int arg0, fd_set *restrict arg1, fd_set *restrict arg2, fd_set *restrict arg3, const struct timespec *restrict arg4, const sigset_t *restrict arg5) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int select(int arg0, fd_set *restrict arg1, fd_set *restrict arg2, fd_set *restrict arg3, struct timeval *restrict arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; errno = ENOSYS; return (int)-1; }
