/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/ipc.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) key_t ftok(const char * arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (key_t)-1; }
