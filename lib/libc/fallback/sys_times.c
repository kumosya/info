/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/times.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) clock_t times(struct tms * arg0) { (void)arg0; errno = ENOSYS; return (clock_t)-1; }
