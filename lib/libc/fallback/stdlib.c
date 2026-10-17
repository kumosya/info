/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int grantpt(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) char * initstate(unsigned int arg0, char * arg1, size_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) int posix_openpt(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) char * ptsname(int arg0) { (void)arg0; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) int ptsname_r(int arg0, char * arg1, size_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int putenv(char * arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) long random(void) { errno = ENOSYS; return (long)-1; }
__attribute__((weak)) char * realpath(const char *restrict arg0, char *restrict arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) void setkey(const char * arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) char * setstate(char * arg0) { (void)arg0; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) void srandom(unsigned int arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) int unlockpt(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
