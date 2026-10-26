/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/resource.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int getrusage(int arg0, struct rusage * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
