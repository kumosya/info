/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int waitid(idtype_t arg0, id_t arg1, siginfo_t * arg2, int arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
