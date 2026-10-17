/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <monetary.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) ssize_t strfmon(char *restrict arg0, size_t arg1, const char *restrict arg2, ...) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) ssize_t strfmon_l(char *restrict arg0, size_t arg1, locale_t arg2, const char *restrict arg3, ...) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (ssize_t)-1; }
