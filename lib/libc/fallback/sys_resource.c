/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/resource.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int getpriority(int arg0, id_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int getrlimit(int arg0, struct rlimit * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int getrusage(int arg0, struct rusage * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int setpriority(int arg0, id_t arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int setrlimit(int arg0, const struct rlimit * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
