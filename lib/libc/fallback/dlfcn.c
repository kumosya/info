/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int dladdr(const void *restrict arg0, Dl_info_t *restrict arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int dlclose(void * arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) char * dlerror(void) { errno = ENOSYS; return (char *)0; }
__attribute__((weak)) void * dlopen(const char * arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (void *)0; }
__attribute__((weak)) void * dlsym(void *restrict arg0, const char *restrict arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (void *)0; }
