/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int mlock(const void * arg0, size_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int mlockall(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int munlock(const void * arg0, size_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int munlockall(void) { errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int posix_madvise(void * arg0, size_t arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int posix_mem_offset(const void *restrict arg0, size_t arg1, off_t *restrict arg2, size_t *restrict arg3, int *restrict arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; return ENOSYS; }
__attribute__((weak)) int posix_typed_mem_get_info(int arg0, struct posix_typed_mem_info * arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int posix_typed_mem_open(const char * arg0, int arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
