#ifndef _POSIX_PTHREAD_H
#define _POSIX_PTHREAD_H


#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef uint64_t pthread_t;
typedef unsigned int pthread_key_t;

typedef struct {
    size_t stack_size;
    int detach_state;
} pthread_attr_t;

typedef struct {
    volatile uint32_t state;
    uint32_t type;
    volatile uint64_t owner;
    uint32_t recursion;
} pthread_mutex_t;

typedef struct {
    int type;
} pthread_mutexattr_t;

typedef struct {
    volatile uint32_t sequence;
} pthread_cond_t;

typedef struct {
    int clock_id;
} pthread_condattr_t;

typedef struct {
    volatile uint32_t state;
} pthread_once_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    uint32_t readers;
    uint32_t waiting_writers;
    uint32_t writer;
} pthread_rwlock_t;

typedef struct {
    int reserved;
} pthread_rwlockattr_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    uint32_t threshold;
    uint32_t waiting;
    uint32_t generation;
} pthread_barrier_t;

typedef struct {
    int reserved;
} pthread_barrierattr_t;

typedef volatile uint32_t pthread_spinlock_t;

#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_KEYS_MAX 64
#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#define PTHREAD_CANCELED ((void*)-1)
#define PTHREAD_STACK_MIN 16384

#define PTHREAD_MUTEX_INITIALIZER {0, PTHREAD_MUTEX_DEFAULT, 0, 0}
#define PTHREAD_COND_INITIALIZER {0}
#define PTHREAD_ONCE_INIT {0}
#define PTHREAD_RWLOCK_INITIALIZER \
    {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0}

#ifdef __cplusplus
extern "C" {
#endif

struct __pthread_cleanup {
    void (*routine)(void*);
    void* argument;
    struct __pthread_cleanup* previous;
};
void __pthread_cleanup_push(struct __pthread_cleanup*, void (*)(void*), void*);
void __pthread_cleanup_pop(struct __pthread_cleanup*, int);
#define pthread_cleanup_push(routine, argument) do { \
    struct __pthread_cleanup __cleanup; \
    __pthread_cleanup_push(&__cleanup, (routine), (argument));
#define pthread_cleanup_pop(execute) \
    __pthread_cleanup_pop(&__cleanup, (execute)); } while (0)

int pthread_attr_init(pthread_attr_t* attr);
int pthread_attr_destroy(pthread_attr_t* attr);
int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* state);
int pthread_attr_setdetachstate(pthread_attr_t* attr, int state);
int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* size);
int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size);

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start)(void*), void* argument);
int pthread_join(pthread_t thread, void** result);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t left, pthread_t right);
void pthread_exit(void* result) __attribute__((noreturn));

int pthread_mutexattr_init(pthread_mutexattr_t* attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t* attr);
int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type);
int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type);
int pthread_mutex_init(pthread_mutex_t* mutex,
                       const pthread_mutexattr_t* attr);
int pthread_mutex_destroy(pthread_mutex_t* mutex);
int pthread_mutex_lock(pthread_mutex_t* mutex);
int pthread_mutex_trylock(pthread_mutex_t* mutex);
int pthread_mutex_timedlock(pthread_mutex_t* mutex,
                            const struct timespec* absolute_time);
int pthread_mutex_unlock(pthread_mutex_t* mutex);

int pthread_condattr_init(pthread_condattr_t* attr);
int pthread_condattr_destroy(pthread_condattr_t* attr);
int pthread_condattr_getclock(const pthread_condattr_t* attr, clockid_t* clock);
int pthread_condattr_setclock(pthread_condattr_t* attr, clockid_t clock);
int pthread_cond_init(pthread_cond_t* condition,
                      const pthread_condattr_t* attr);
int pthread_cond_destroy(pthread_cond_t* condition);
int pthread_cond_wait(pthread_cond_t* condition, pthread_mutex_t* mutex);
int pthread_cond_timedwait(pthread_cond_t* condition, pthread_mutex_t* mutex,
                           const struct timespec* absolute_time);
int pthread_cond_signal(pthread_cond_t* condition);
int pthread_cond_broadcast(pthread_cond_t* condition);

int pthread_once(pthread_once_t* once, void (*initializer)(void));
int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
void* pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);

int pthread_rwlock_init(pthread_rwlock_t* lock,
                        const pthread_rwlockattr_t* attr);
int pthread_rwlock_destroy(pthread_rwlock_t* lock);
int pthread_rwlock_rdlock(pthread_rwlock_t* lock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t* lock);
int pthread_rwlock_wrlock(pthread_rwlock_t* lock);
int pthread_rwlock_trywrlock(pthread_rwlock_t* lock);
int pthread_rwlock_unlock(pthread_rwlock_t* lock);

int pthread_barrier_init(pthread_barrier_t* barrier,
                         const pthread_barrierattr_t* attr,
                         unsigned int count);
int pthread_barrier_destroy(pthread_barrier_t* barrier);
int pthread_barrier_wait(pthread_barrier_t* barrier);

int pthread_spin_init(pthread_spinlock_t* lock, int task_shared);
int pthread_spin_destroy(pthread_spinlock_t* lock);
int pthread_spin_lock(pthread_spinlock_t* lock);
int pthread_spin_trylock(pthread_spinlock_t* lock);
int pthread_spin_unlock(pthread_spinlock_t* lock);

int pthread_cancel(pthread_t thread);
int pthread_setcancelstate(int state, int* old_state);
int pthread_setcanceltype(int type, int* old_type);
void pthread_testcancel(void);

#ifdef __cplusplus
}
#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_pthread_t
#define __DEFINED_pthread_key_t
#define __DEFINED_pthread_attr_t
#define __DEFINED_pthread_mutex_t
#define __DEFINED_pthread_mutexattr_t
#define __DEFINED_pthread_cond_t
#define __DEFINED_pthread_condattr_t
#define __DEFINED_pthread_once_t
#define __DEFINED_pthread_rwlock_t
#define __DEFINED_pthread_rwlockattr_t
#define __DEFINED_pthread_barrier_t
#define __DEFINED_pthread_barrierattr_t
#define __DEFINED_pthread_spinlock_t

/* Additional POSIX types. */
#include <sched.h>

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int pthread_atfork(void (*)(void), void (*)(void), void(*)(void));
int pthread_attr_getguardsize(const pthread_attr_t *__restrict, size_t *__restrict);
int pthread_attr_getinheritsched(const pthread_attr_t *__restrict, int *__restrict);
int pthread_attr_getschedparam(const pthread_attr_t *__restrict, struct sched_param *__restrict);
int pthread_attr_getschedpolicy(const pthread_attr_t *__restrict, int *__restrict);
int pthread_attr_getscope(const pthread_attr_t *__restrict, int *__restrict);
int pthread_attr_getstack(const pthread_attr_t *__restrict, void **__restrict, size_t *__restrict);
int pthread_attr_setguardsize(pthread_attr_t *, size_t);
int pthread_attr_setinheritsched(pthread_attr_t *, int);
int pthread_attr_setschedparam(pthread_attr_t *__restrict, const struct sched_param *__restrict);
int pthread_attr_setschedpolicy(pthread_attr_t *, int);
int pthread_attr_setscope(pthread_attr_t *, int);
int pthread_attr_setstack(pthread_attr_t *, void *, size_t);
int pthread_barrierattr_destroy(pthread_barrierattr_t *);
int pthread_barrierattr_getpshared( const pthread_barrierattr_t *__restrict, int *__restrict);
int pthread_barrierattr_init(pthread_barrierattr_t *);
int pthread_barrierattr_setpshared(pthread_barrierattr_t *, int);
int pthread_cond_clockwait(pthread_cond_t *__restrict, pthread_mutex_t *__restrict, clockid_t, const struct timespec *__restrict);
int pthread_condattr_getpshared(const pthread_condattr_t *__restrict, int *__restrict);
int pthread_condattr_setpshared(pthread_condattr_t *, int);
int pthread_getcpuclockid(pthread_t, clockid_t *);
int pthread_getschedparam(pthread_t, int *__restrict, struct sched_param *__restrict);
int pthread_mutex_clocklock(pthread_mutex_t *__restrict, clockid_t, const struct timespec *__restrict);
int pthread_mutex_consistent(pthread_mutex_t *);
int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict, int *__restrict);
int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict, int, int *__restrict);
int pthread_mutexattr_getprioceiling( const pthread_mutexattr_t *__restrict, int *__restrict);
int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *__restrict, int *__restrict);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *__restrict, int *__restrict);
int pthread_mutexattr_getrobust(const pthread_mutexattr_t *__restrict, int *__restrict);
int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *, int);
int pthread_mutexattr_setprotocol(pthread_mutexattr_t *, int);
int pthread_mutexattr_setpshared(pthread_mutexattr_t *, int);
int pthread_mutexattr_setrobust(pthread_mutexattr_t *, int);
int pthread_rwlock_clockrdlock(pthread_rwlock_t *__restrict, clockid_t, const struct timespec *__restrict);
int pthread_rwlock_clockwrlock(pthread_rwlock_t *__restrict, clockid_t, const struct timespec *__restrict);
int pthread_rwlock_timedrdlock(pthread_rwlock_t *__restrict, const struct timespec *__restrict);
int pthread_rwlock_timedwrlock(pthread_rwlock_t *__restrict, const struct timespec *__restrict);
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *);
int pthread_rwlockattr_getpshared( const pthread_rwlockattr_t *__restrict, int *__restrict);
int pthread_rwlockattr_init(pthread_rwlockattr_t *);
int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *, int);
int pthread_setschedparam(pthread_t, int, const struct sched_param *);
int pthread_setschedprio(pthread_t, int);
#ifdef __cplusplus
}
#endif

#endif
