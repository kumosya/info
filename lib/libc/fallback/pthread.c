/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int pthread_attr_getguardsize(const pthread_attr_t *restrict arg0, size_t *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_getinheritsched(const pthread_attr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_getschedparam(const pthread_attr_t *restrict arg0, struct sched_param *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_getschedpolicy(const pthread_attr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_getscope(const pthread_attr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_getstack(const pthread_attr_t *restrict arg0, void **restrict arg1, size_t *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int pthread_attr_setguardsize(pthread_attr_t * arg0, size_t arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_setinheritsched(pthread_attr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_setschedparam(pthread_attr_t *restrict arg0, const struct sched_param *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_setschedpolicy(pthread_attr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_setscope(pthread_attr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_attr_setstack(pthread_attr_t * arg0, void * arg1, size_t arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int pthread_barrierattr_destroy(pthread_barrierattr_t * arg0) { (void)arg0; return ENOSYS; }
__attribute__((weak)) int pthread_barrierattr_getpshared(const pthread_barrierattr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_barrierattr_init(pthread_barrierattr_t * arg0) { (void)arg0; return ENOSYS; }
__attribute__((weak)) int pthread_barrierattr_setpshared(pthread_barrierattr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_cond_clockwait(pthread_cond_t *restrict arg0, pthread_mutex_t *restrict arg1, clockid_t arg2, const struct timespec *restrict arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; return ENOSYS; }
__attribute__((weak)) int pthread_condattr_getpshared(const pthread_condattr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_condattr_setpshared(pthread_condattr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_getcpuclockid(pthread_t arg0, clockid_t * arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_getschedparam(pthread_t arg0, int *restrict arg1, struct sched_param *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int pthread_mutex_clocklock(pthread_mutex_t *restrict arg0, clockid_t arg1, const struct timespec *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int pthread_mutex_consistent(pthread_mutex_t * arg0) { (void)arg0; return ENOSYS; }
__attribute__((weak)) int pthread_mutex_getprioceiling(const pthread_mutex_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_mutex_setprioceiling(pthread_mutex_t *restrict arg0, int arg1, int *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_mutexattr_getpshared(const pthread_mutexattr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_mutexattr_getrobust(const pthread_mutexattr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_mutexattr_setprioceiling(pthread_mutexattr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_mutexattr_setprotocol(pthread_mutexattr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_mutexattr_setpshared(pthread_mutexattr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_mutexattr_setrobust(pthread_mutexattr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_rwlock_clockrdlock(pthread_rwlock_t *restrict arg0, clockid_t arg1, const struct timespec *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int pthread_rwlock_clockwrlock(pthread_rwlock_t *restrict arg0, clockid_t arg1, const struct timespec *restrict arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int pthread_rwlock_timedrdlock(pthread_rwlock_t *restrict arg0, const struct timespec *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_rwlock_timedwrlock(pthread_rwlock_t *restrict arg0, const struct timespec *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_rwlockattr_destroy(pthread_rwlockattr_t * arg0) { (void)arg0; return ENOSYS; }
__attribute__((weak)) int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *restrict arg0, int *restrict arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_rwlockattr_init(pthread_rwlockattr_t * arg0) { (void)arg0; return ENOSYS; }
__attribute__((weak)) int pthread_rwlockattr_setpshared(pthread_rwlockattr_t * arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
__attribute__((weak)) int pthread_setschedparam(pthread_t arg0, int arg1, const struct sched_param * arg2) { (void)arg0; (void)arg1; (void)arg2; return ENOSYS; }
__attribute__((weak)) int pthread_setschedprio(pthread_t arg0, int arg1) { (void)arg0; (void)arg1; return ENOSYS; }
