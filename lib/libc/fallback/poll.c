/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <poll.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int poll(struct pollfd * arg0, nfds_t arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int ppoll(struct pollfd * arg0, nfds_t arg1, const struct timespec *restrict arg2, const sigset_t *restrict arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
