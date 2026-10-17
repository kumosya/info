/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <wordexp.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int wordexp(const char *restrict arg0, wordexp_t *restrict arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) void wordfree(wordexp_t * arg0) { (void)arg0; errno = ENOSYS;  }
