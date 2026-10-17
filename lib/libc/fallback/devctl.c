/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <devctl.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int posix_devctl(int arg0, int arg1, void *restrict arg2, size_t arg3, int *restrict arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; return ENOSYS; }
