/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sys/msg.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int msgctl(int arg0, int arg1, struct msqid_ds * arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int msgget(key_t arg0, int arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) ssize_t msgrcv(int arg0, void * arg1, size_t arg2, long arg3, int arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; errno = ENOSYS; return (ssize_t)-1; }
__attribute__((weak)) int msgsnd(int arg0, const void * arg1, size_t arg2, int arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
