/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <nl_types.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int catclose(nl_catd arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) char * catgets(nl_catd arg0, int arg1, int arg2, const char * arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (char *)arg3; }
__attribute__((weak)) nl_catd catopen(const char * arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (nl_catd)-1; }
