/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <time.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) clock_t clock(void) { errno = ENOSYS; return (clock_t)-1; }
__attribute__((weak)) int clock_getcpuclockid(pid_t arg0, clockid_t * arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int clock_settime(clockid_t arg0, const struct timespec * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) struct tm * getdate(const char * arg0) { (void)arg0; errno = ENOSYS; return (struct tm *)0; }
__attribute__((weak)) int timer_create(clockid_t arg0, struct sigevent *restrict arg1, timer_t *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int timer_delete(timer_t arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int timer_getoverrun(timer_t arg0) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int timer_gettime(timer_t arg0, struct itimerspec * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int timer_settime(timer_t arg0, int arg1, const struct itimerspec *restrict arg2, struct itimerspec *restrict arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
