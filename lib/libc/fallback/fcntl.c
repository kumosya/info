/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int posix_fadvise(int arg0, off_t arg1, off_t arg2, int arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; return ENOSYS; }
__attribute__((weak)) int posix_fallocate(int arg0, off_t arg1, off_t arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
