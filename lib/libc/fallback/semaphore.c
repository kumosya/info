/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <semaphore.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int sem_clockwait(sem_t *restrict arg0, clockid_t arg1, const struct timespec *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
