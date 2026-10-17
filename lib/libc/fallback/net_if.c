/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <net/if.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) void if_freenameindex(struct if_nameindex * arg0) { (void)arg0; errno = ENOSYS;  }
__attribute__((weak)) char * if_indextoname(unsigned int arg0, char * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) struct if_nameindex * if_nameindex(void) { errno = ENOSYS; return (struct if_nameindex *)0; }
__attribute__((weak)) unsigned if_nametoindex(const char * arg0) { (void)arg0; errno = ENOSYS; return (unsigned)-1; }
