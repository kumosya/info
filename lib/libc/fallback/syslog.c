/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <syslog.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) void closelog(void) { errno = ENOSYS;  }
__attribute__((weak)) void openlog(const char * arg0, int arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS;  }
__attribute__((weak)) int setlogmask(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) void syslog(int arg0, const char * arg1, ...) { (void)arg0; (void)arg1; errno = ENOSYS;  }
