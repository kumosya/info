/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <ftw.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int nftw(const char * arg0, int (* arg1)(const char *, const struct stat *, int, struct FTW *), int arg2, int arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
