/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/sem.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int semctl(int arg0, int arg1, int arg2, ...) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int semget(key_t arg0, int arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int semop(int arg0, struct sembuf * arg1, size_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
