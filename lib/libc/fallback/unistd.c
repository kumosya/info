/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <unistd.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) unsigned alarm(unsigned int arg0) { (void)arg0; errno = ENOSYS; return (unsigned)-1; }
__attribute__((weak)) char * crypt(const char * arg0, const char * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) void encrypt(char * arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS;  }
__attribute__((weak)) int fexecve(int arg0, char *const * arg1, char *const * arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int getentropy(void * arg0, size_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int getgroups(int arg0, gid_t * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) long gethostid(void) { errno = ENOSYS; return (long)-1; }
__attribute__((weak)) int getresgid(gid_t *restrict arg0, gid_t *restrict arg1, gid_t *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int getresuid(uid_t *restrict arg0, uid_t *restrict arg1, uid_t *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int lockf(int arg0, int arg1, off_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int nice(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int posix_close(int arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) ssize_t readlinkat(int arg0, const char *restrict arg1, char *restrict arg2, size_t arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) int setegid(gid_t arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int seteuid(uid_t arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int setregid(gid_t arg0, gid_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int setresgid(gid_t arg0, gid_t arg1, gid_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int setresuid(uid_t arg0, uid_t arg1, uid_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int setreuid(uid_t arg0, uid_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) void swab(const void *restrict arg0, void *restrict arg1, ssize_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS;  }
__attribute__((weak)) int symlinkat(const char * arg0, int arg1, const char * arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) void sync(void) { errno = ENOSYS;  }
__attribute__((weak)) pid_t tcgetpgrp(int arg0) { (void)arg0; errno = ENOSYS; return (pid_t)-1; }
__attribute__((weak)) int tcsetpgrp(int arg0, pid_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) char * ttyname(int arg0) { (void)arg0; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) int ttyname_r(int arg0, char * arg1, size_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
