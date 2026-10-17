/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/statvfs.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int fstatvfs(int arg0, struct statvfs * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int statvfs(const char *restrict arg0, struct statvfs *restrict arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
