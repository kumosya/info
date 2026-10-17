/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <strings.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int ffs(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int ffsl(long arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int ffsll(long long arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
