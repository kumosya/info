/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) char * ctermid(char * arg0) { (void)arg0; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) void flockfile(FILE * arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) FILE * fmemopen(void *restrict arg0, size_t arg1, const char *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (FILE *)0; }
__attribute__((weak)) int ftrylockfile(FILE * arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) void funlockfile(FILE * arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) FILE * open_memstream(char ** arg0, size_t * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (FILE *)0; }
__attribute__((weak)) int pclose(FILE * arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) FILE * popen(const char * arg0, const char * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (FILE *)0; }
__attribute__((weak)) void setbuf(FILE *restrict arg0, char *restrict arg1) { (void)arg0; (void)arg1; errno = ENOSYS;  }
__attribute__((weak)) int setvbuf(FILE *restrict arg0, char *restrict arg1, int arg2, size_t arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) FILE * tmpfile(void) { errno = ENOSYS; return (FILE *)0; }
__attribute__((weak)) char * tmpnam(char * arg0) { (void)arg0; errno = ENOSYS; return (char *)0; }
