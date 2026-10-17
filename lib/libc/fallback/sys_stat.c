/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int mkfifo(const char * arg0, mode_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int mkfifoat(int arg0, const char * arg1, mode_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int mknod(const char * arg0, mode_t arg1, dev_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int mknodat(int arg0, const char * arg1, mode_t arg2, dev_t arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
