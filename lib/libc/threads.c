#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

#include "os/c_api.h"

typedef struct {
    thrd_start_t start;
    void* argument;
} ThrdStart;

static void* run_thrd(void* opaque) {
    ThrdStart* state = (ThrdStart*)opaque;
    thrd_start_t start = state->start;
    void* argument = state->argument;
    int result;
    free(state);
    result = start(argument);
    return (void*)(intptr_t)result;
}

static int map_result(int result) {
    if (result == 0) return thrd_success;
    if (result == ENOMEM || result == EAGAIN) return thrd_nomem;
    if (result == ETIMEDOUT) return thrd_timedout;
    if (result == EBUSY) return thrd_busy;
    return thrd_error;
}

int thrd_create(thrd_t* thread, thrd_start_t start, void* argument) {
    ThrdStart* state;
    int result;
    if (thread == NULL || start == NULL) return thrd_error;
    state = (ThrdStart*)malloc(sizeof(*state));
    if (state == NULL) return thrd_nomem;
    state->start = start;
    state->argument = argument;
    result = pthread_create(thread, NULL, run_thrd, state);
    if (result != 0) free(state);
    return map_result(result);
}

thrd_t thrd_current(void) { return pthread_self(); }

int thrd_equal(thrd_t left, thrd_t right) {
    return pthread_equal(left, right);
}

int thrd_join(thrd_t thread, int* result) {
    void* value = NULL;
    int status = pthread_join(thread, &value);
    if (status == 0 && result != NULL) *result = (int)(intptr_t)value;
    return map_result(status);
}

int thrd_detach(thrd_t thread) { return map_result(pthread_detach(thread)); }

void thrd_exit(int result) { pthread_exit((void*)(intptr_t)result); }

int thrd_sleep(const struct timespec* duration, struct timespec* remaining) {
    return nanosleep(duration, remaining) == 0 ? 0 : -1;
}

void thrd_yield(void) { os_yield(); }

int mtx_init(mtx_t* mutex, int type) {
    pthread_mutexattr_t attr;
    if (type & ~(mtx_recursive | mtx_timed)) return thrd_error;
    pthread_mutexattr_init(&attr);
    if (type & mtx_recursive) attr.type = PTHREAD_MUTEX_RECURSIVE;
    return map_result(pthread_mutex_init(mutex, &attr));
}

void mtx_destroy(mtx_t* mutex) { (void)pthread_mutex_destroy(mutex); }
int mtx_lock(mtx_t* mutex) { return map_result(pthread_mutex_lock(mutex)); }
int mtx_trylock(mtx_t* mutex) {
    return map_result(pthread_mutex_trylock(mutex));
}
int mtx_timedlock(mtx_t* mutex, const struct timespec* absolute_time) {
    return map_result(pthread_mutex_timedlock(mutex, absolute_time));
}
int mtx_unlock(mtx_t* mutex) { return map_result(pthread_mutex_unlock(mutex)); }

int cnd_init(cnd_t* condition) {
    return map_result(pthread_cond_init(condition, NULL));
}
void cnd_destroy(cnd_t* condition) { (void)pthread_cond_destroy(condition); }
int cnd_wait(cnd_t* condition, mtx_t* mutex) {
    return map_result(pthread_cond_wait(condition, mutex));
}
int cnd_timedwait(cnd_t* condition, mtx_t* mutex,
                  const struct timespec* absolute_time) {
    return map_result(pthread_cond_timedwait(condition, mutex, absolute_time));
}
int cnd_signal(cnd_t* condition) {
    return map_result(pthread_cond_signal(condition));
}
int cnd_broadcast(cnd_t* condition) {
    return map_result(pthread_cond_broadcast(condition));
}

void call_once(once_flag* flag, void (*function)(void)) {
    (void)pthread_once(flag, function);
}
int tss_create(tss_t* key, void (*destructor)(void*)) {
    return map_result(pthread_key_create(key, destructor));
}
void tss_delete(tss_t key) { (void)pthread_key_delete(key); }
void* tss_get(tss_t key) { return pthread_getspecific(key); }
int tss_set(tss_t key, void* value) {
    return map_result(pthread_setspecific(key, value));
}
