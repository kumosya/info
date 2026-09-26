#pragma once

#include <pthread.h>
#include <time.h>

typedef pthread_t thrd_t;
typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
typedef pthread_once_t once_flag;
typedef pthread_key_t tss_t;
typedef int (*thrd_start_t)(void*);

enum {
    thrd_success = 0,
    thrd_nomem,
    thrd_timedout,
    thrd_busy,
    thrd_error
};

enum {
    mtx_plain = 0,
    mtx_recursive = 1,
    mtx_timed = 2
};

#define ONCE_FLAG_INIT PTHREAD_ONCE_INIT
#define TSS_DTOR_ITERATIONS PTHREAD_DESTRUCTOR_ITERATIONS

#ifdef __cplusplus
extern "C" {
#endif

int thrd_create(thrd_t* thread, thrd_start_t start, void* argument);
thrd_t thrd_current(void);
int thrd_equal(thrd_t left, thrd_t right);
int thrd_join(thrd_t thread, int* result);
int thrd_detach(thrd_t thread);
void thrd_exit(int result) __attribute__((noreturn));
int thrd_sleep(const struct timespec* duration, struct timespec* remaining);
void thrd_yield(void);

int mtx_init(mtx_t* mutex, int type);
void mtx_destroy(mtx_t* mutex);
int mtx_lock(mtx_t* mutex);
int mtx_trylock(mtx_t* mutex);
int mtx_timedlock(mtx_t* mutex, const struct timespec* absolute_time);
int mtx_unlock(mtx_t* mutex);

int cnd_init(cnd_t* condition);
void cnd_destroy(cnd_t* condition);
int cnd_wait(cnd_t* condition, mtx_t* mutex);
int cnd_timedwait(cnd_t* condition, mtx_t* mutex,
                  const struct timespec* absolute_time);
int cnd_signal(cnd_t* condition);
int cnd_broadcast(cnd_t* condition);

void call_once(once_flag* flag, void (*function)(void));
int tss_create(tss_t* key, void (*destructor)(void*));
void tss_delete(tss_t key);
void* tss_get(tss_t key);
int tss_set(tss_t key, void* value);

#ifdef __cplusplus
}
#endif
