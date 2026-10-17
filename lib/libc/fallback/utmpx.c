/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <utmpx.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) void endutxent(void) { errno = ENOSYS;  }
__attribute__((weak)) struct utmpx * getutxent(void) { errno = ENOSYS; return (struct utmpx *)0; }
__attribute__((weak)) struct utmpx * getutxid(const struct utmpx * arg0) { (void)arg0; errno = ENOSYS; return (struct utmpx *)0; }
__attribute__((weak)) struct utmpx * getutxline(const struct utmpx * arg0) { (void)arg0; errno = ENOSYS; return (struct utmpx *)0; }
__attribute__((weak)) struct utmpx * pututxline(const struct utmpx * arg0) { (void)arg0; errno = ENOSYS; return (struct utmpx *)0; }
__attribute__((weak)) void setutxent(void) { errno = ENOSYS;  }
