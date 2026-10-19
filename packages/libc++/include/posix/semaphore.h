#ifndef _POSIX_SEMAPHORE_H
#define _POSIX_SEMAPHORE_H


#include <stdint.h>
#include <time.h>

typedef struct {
    volatile uint32_t value;
    uint32_t initialized;
} sem_t;

#define SEM_FAILED ((sem_t*)-1)
#define SEM_VALUE_MAX 2147483647

#ifdef __cplusplus
extern "C" {
#endif

int sem_init(sem_t* semaphore, int task_shared, unsigned int value);
int sem_destroy(sem_t* semaphore);
int sem_wait(sem_t* semaphore);
int sem_trywait(sem_t* semaphore);
int sem_timedwait(sem_t* semaphore, const struct timespec* absolute_time);
int sem_post(sem_t* semaphore);
int sem_getvalue(sem_t* semaphore, int* value);
sem_t* sem_open(const char* name, int flags, ...);
int sem_close(sem_t* semaphore);
int sem_unlink(const char* name);

#ifdef __cplusplus
}
#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_sem_t

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int sem_clockwait(sem_t *__restrict, clockid_t, const struct timespec *__restrict);
#ifdef __cplusplus
}
#endif

#endif
