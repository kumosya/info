/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <fmtmsg.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int fmtmsg(long arg0, const char * arg1, int arg2, const char * arg3, const char * arg4, const char * arg5) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; errno = ENOSYS; return (int)-1; }
