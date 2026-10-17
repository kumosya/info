/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/shm.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) void * shmat(int arg0, const void * arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (void *)-1; }
__attribute__((weak)) int shmctl(int arg0, int arg1, struct shmid_ds * arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int shmdt(const void * arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int shmget(key_t arg0, size_t arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
