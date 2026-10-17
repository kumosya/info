/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <sched.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int sched_get_priority_max(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int sched_get_priority_min(int arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int sched_getparam(pid_t arg0, struct sched_param * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int sched_getscheduler(pid_t arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int sched_rr_get_interval(pid_t arg0, struct timespec * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int sched_setparam(pid_t arg0, const struct sched_param * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int sched_setscheduler(pid_t arg0, int arg1, const struct sched_param * arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
