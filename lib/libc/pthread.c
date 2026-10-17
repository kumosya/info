#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "os_adapter.h"

#define LIBC_THREAD_CAPACITY 64
#define LIBC_DEFAULT_STACK_SIZE (64U * 1024U)
extern void __libc_release_errno(void);

typedef struct {
    uint32_t used;
    uint32_t detached;
    uint32_t join_claimed;
    pthread_t id;
    void* stack;
    size_t stack_size;
    void* (*start)(void*);
    void* argument;
    void* result;
} ThreadControl;

typedef struct {
    uint32_t used;
    pthread_t id;
    struct __pthread_cleanup* cleanup;
    void* values[PTHREAD_KEYS_MAX];
} ThreadSpecificValues;

typedef struct {
    uint32_t used;
    void (*destructor)(void*);
} ThreadKey;

static ThreadControl controls[LIBC_THREAD_CAPACITY];
static ThreadSpecificValues specific_values[LIBC_THREAD_CAPACITY];
static ThreadKey keys[PTHREAD_KEYS_MAX];
static volatile uint8_t registry_lock;

static void lock_registry(void) {
    while (__atomic_test_and_set(&registry_lock, __ATOMIC_ACQUIRE)) {
        __libc_os_yield();
    }
}

static void unlock_registry(void) {
    __atomic_clear(&registry_lock, __ATOMIC_RELEASE);
}

static ThreadControl* find_control(pthread_t id) {
    size_t index;
    for (index = 0; index < LIBC_THREAD_CAPACITY; ++index) {
        if (controls[index].used && controls[index].id == id) {
            return &controls[index];
        }
    }
    return NULL;
}

static void release_control(ThreadControl* control) {
    void* stack;
    if (control == NULL) return;
    lock_registry();
    stack = control->stack;
    memset(control, 0, sizeof(*control));
    unlock_registry();
    free(stack);
}

static void reap_detached(void) {
    size_t index;
    for (index = 0; index < LIBC_THREAD_CAPACITY; ++index) {
        int exit_code;
        ThreadControl* control = &controls[index];
        lock_registry();
        if (!control->used || !control->detached || control->id == 0) {
            unlock_registry();
            continue;
        }
        unlock_registry();
        if (__libc_os_wait_thread(control->id, &exit_code, 1) == 0) {
            release_control(control);
        }
    }
}

static ThreadSpecificValues* find_specific(pthread_t id, int create) {
    size_t index;
    ThreadSpecificValues* free_slot = NULL;
    for (index = 0; index < LIBC_THREAD_CAPACITY; ++index) {
        if (specific_values[index].used && specific_values[index].id == id) {
            return &specific_values[index];
        }
        if (!specific_values[index].used && free_slot == NULL) {
            free_slot = &specific_values[index];
        }
    }
    if (create && free_slot != NULL) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = 1;
        free_slot->id = id;
        return free_slot;
    }
    return NULL;
}

void __pthread_cleanup_push(struct __pthread_cleanup* node, void (*routine)(void*), void* argument) {
    *node=(struct __pthread_cleanup){routine,argument,NULL};
    lock_registry();
    ThreadSpecificValues* values=find_specific(pthread_self(),1);
    if (values) { node->previous=values->cleanup; values->cleanup=node; }
    unlock_registry();
}
void __pthread_cleanup_pop(struct __pthread_cleanup* node,int execute) {
    lock_registry();
    ThreadSpecificValues* values=find_specific(pthread_self(),0);
    if(values && values->cleanup==node) values->cleanup=node->previous;
    unlock_registry();
    if(execute) node->routine(node->argument);
}
static void run_cleanup(void) {
    while(1) {
        lock_registry(); ThreadSpecificValues* values=find_specific(pthread_self(),0);
        struct __pthread_cleanup* node=values?values->cleanup:NULL;
        if(node) values->cleanup=node->previous;
        unlock_registry(); if(!node) return;
        node->routine(node->argument);
    }
}

static void run_specific_destructors(void) {
    pthread_t id = pthread_self();
    unsigned int pass;
    for (pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; ++pass) {
        unsigned int key;
        int called = 0;
        for (key = 0; key < PTHREAD_KEYS_MAX; ++key) {
            void* value = NULL;
            void (*destructor)(void*) = NULL;
            lock_registry();
            {
                ThreadSpecificValues* values = find_specific(id, 0);
                if (values != NULL && keys[key].used &&
                    values->values[key] != NULL &&
                    keys[key].destructor != NULL) {
                    value = values->values[key];
                    values->values[key] = NULL;
                    destructor = keys[key].destructor;
                }
            }
            unlock_registry();
            if (destructor != NULL) {
                destructor(value);
                called = 1;
            }
        }
        if (!called) break;
    }
    lock_registry();
    {
        ThreadSpecificValues* values = find_specific(id, 0);
        if (values != NULL) memset(values, 0, sizeof(*values));
    }
    unlock_registry();
}

static void thread_entry(void* argument) {
    ThreadControl* control = (ThreadControl*)argument;
    void* result = control->start(control->argument);
    run_specific_destructors();
    control->result = result;
    __libc_release_errno();
    __libc_os_exit_thread(0);
}

static int valid_time(const struct timespec* value) {
    return value != NULL && value->tv_sec >= 0 && value->tv_nsec >= 0 &&
           value->tv_nsec < 1000000000L;
}

static int deadline_reached(const struct timespec* absolute_time) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return 1;
    return now.tv_sec > absolute_time->tv_sec ||
           (now.tv_sec == absolute_time->tv_sec &&
            now.tv_nsec >= absolute_time->tv_nsec);
}

int pthread_attr_init(pthread_attr_t* attr) {
    if (attr == NULL) return EINVAL;
    attr->stack_size = LIBC_DEFAULT_STACK_SIZE;
    attr->detach_state = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t* attr) {
    if (attr == NULL) return EINVAL;
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t* attr, int* state) {
    if (attr == NULL || state == NULL) return EINVAL;
    *state = attr->detach_state;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t* attr, int state) {
    if (attr == NULL || (state != PTHREAD_CREATE_JOINABLE &&
                         state != PTHREAD_CREATE_DETACHED)) return EINVAL;
    attr->detach_state = state;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t* attr, size_t* size) {
    if (attr == NULL || size == NULL) return EINVAL;
    *size = attr->stack_size;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t* attr, size_t size) {
    if (attr == NULL || size < PTHREAD_STACK_MIN) return EINVAL;
    attr->stack_size = size;
    return 0;
}

int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start)(void*), void* argument) {
    ThreadControl* control = NULL;
    size_t stack_size = attr == NULL ? LIBC_DEFAULT_STACK_SIZE : attr->stack_size;
    int detached = attr == NULL ? PTHREAD_CREATE_JOINABLE : attr->detach_state;
    size_t index;
    uint64_t stack_top;
    if (thread == NULL || start == NULL || stack_size < PTHREAD_STACK_MIN ||
        (detached != PTHREAD_CREATE_JOINABLE &&
         detached != PTHREAD_CREATE_DETACHED)) return EINVAL;

    reap_detached();
    lock_registry();
    for (index = 0; index < LIBC_THREAD_CAPACITY; ++index) {
        if (!controls[index].used) {
            control = &controls[index];
            memset(control, 0, sizeof(*control));
            control->used = 1;
            break;
        }
    }
    unlock_registry();
    if (control == NULL) return EAGAIN;

    control->stack = malloc(stack_size);
    if (control->stack == NULL) {
        release_control(control);
        return EAGAIN;
    }
    control->stack_size = stack_size;
    control->detached = detached == PTHREAD_CREATE_DETACHED;
    control->start = start;
    control->argument = argument;
    stack_top = ((uint64_t)(uintptr_t)control->stack + stack_size) & ~15ULL;
    control->id = __libc_os_create_thread(thread_entry, control, stack_top);
    if (control->id == 0) {
        release_control(control);
        return EAGAIN;
    }
    *thread = control->id;
    return 0;
}

int pthread_join(pthread_t thread, void** result) {
    ThreadControl* control;
    int exit_code;
    if (thread == pthread_self()) return EDEADLK;
    lock_registry();
    control = find_control(thread);
    if (control == NULL) {
        unlock_registry();
        return ESRCH;
    }
    if (control->detached || control->join_claimed) {
        unlock_registry();
        return EINVAL;
    }
    control->join_claimed = 1;
    unlock_registry();
    if (__libc_os_wait_thread(thread, &exit_code, 0) != 0) {
        lock_registry();
        control->join_claimed = 0;
        unlock_registry();
        return ESRCH;
    }
    if (result != NULL) *result = control->result;
    release_control(control);
    return 0;
}

int pthread_detach(pthread_t thread) {
    ThreadControl* control;
    int exit_code;
    lock_registry();
    control = find_control(thread);
    if (control == NULL) {
        unlock_registry();
        return ESRCH;
    }
    if (control->detached || control->join_claimed) {
        unlock_registry();
        return EINVAL;
    }
    control->detached = 1;
    unlock_registry();
    if (__libc_os_wait_thread(thread, &exit_code, 1) == 0) {
        release_control(control);
    }
    return 0;
}

pthread_t pthread_self(void) { return __libc_os_get_thread_id(); }

int pthread_equal(pthread_t left, pthread_t right) { return left == right; }

void pthread_exit(void* result) {
    ThreadControl* control;
    run_cleanup();
    run_specific_destructors();
    lock_registry();
    control = find_control(pthread_self());
    if (control != NULL) control->result = result;
    unlock_registry();
    __libc_release_errno();
    __libc_os_exit_thread(0);
}

int pthread_mutexattr_init(pthread_mutexattr_t* attr) {
    if (attr == NULL) return EINVAL;
    attr->type = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) {
    if (attr == NULL) return EINVAL;
    attr->type = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type) {
    if (attr == NULL || type == NULL) return EINVAL;
    *type = attr->type;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type) {
    if (attr == NULL || type < PTHREAD_MUTEX_NORMAL ||
        type > PTHREAD_MUTEX_ERRORCHECK) return EINVAL;
    attr->type = type;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex,
                       const pthread_mutexattr_t* attr) {
    if (mutex == NULL) return EINVAL;
    memset(mutex, 0, sizeof(*mutex));
    mutex->type = attr == NULL ? PTHREAD_MUTEX_DEFAULT : attr->type;
    if (mutex->type > PTHREAD_MUTEX_ERRORCHECK) return EINVAL;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
    if (mutex == NULL) return EINVAL;
    if (__atomic_load_n(&mutex->state, __ATOMIC_ACQUIRE) != 0) return EBUSY;
    memset(mutex, 0, sizeof(*mutex));
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
    uint32_t expected = 0;
    pthread_t self;
    if (mutex == NULL) return EINVAL;
    self = pthread_self();
    if (__atomic_load_n(&mutex->owner, __ATOMIC_ACQUIRE) == self) {
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
            ++mutex->recursion;
            return 0;
        }
        return EBUSY;
    }
    if (!__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return EBUSY;
    }
    __atomic_store_n(&mutex->owner, self, __ATOMIC_RELEASE);
    mutex->recursion = 1;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    uint32_t expected = 0;
    pthread_t self;
    if (mutex == NULL) return EINVAL;
    self = pthread_self();
    if (__atomic_load_n(&mutex->owner, __ATOMIC_ACQUIRE) == self) {
        if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
            ++mutex->recursion;
            return 0;
        }
        if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) return EDEADLK;
    }
    if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_store_n(&mutex->owner, self, __ATOMIC_RELEASE);
        mutex->recursion = 1;
        return 0;
    }
    while (__atomic_exchange_n(&mutex->state, 2, __ATOMIC_ACQUIRE) != 0) {
        __libc_os_wait_address(&mutex->state, 2);
    }
    __atomic_store_n(&mutex->owner, self, __ATOMIC_RELEASE);
    mutex->recursion = 1;
    return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t* mutex,
                            const struct timespec* absolute_time) {
    int result;
    if (!valid_time(absolute_time)) return EINVAL;
    while ((result = pthread_mutex_trylock(mutex)) == EBUSY) {
        if (deadline_reached(absolute_time)) return ETIMEDOUT;
        __libc_os_sleep(1);
    }
    return result;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    uint32_t previous;
    if (mutex == NULL) return EINVAL;
    if (__atomic_load_n(&mutex->owner, __ATOMIC_ACQUIRE) != pthread_self()) {
        return EPERM;
    }
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->recursion > 1) {
        --mutex->recursion;
        return 0;
    }
    mutex->recursion = 0;
    __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELEASE);
    previous = __atomic_exchange_n(&mutex->state, 0, __ATOMIC_RELEASE);
    if (previous == 2) __libc_os_wake_address(&mutex->state, 1);
    return 0;
}

int pthread_condattr_init(pthread_condattr_t* attr) {
    if (attr == NULL) return EINVAL;
    attr->clock_id = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t* attr) {
    if (attr == NULL) return EINVAL;
    attr->clock_id = CLOCK_REALTIME;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t* attr, clockid_t* clock) {
    if (attr == NULL || clock == NULL) return EINVAL;
    *clock = attr->clock_id;
    return 0;
}

int pthread_condattr_setclock(pthread_condattr_t* attr, clockid_t clock) {
    if (attr == NULL || (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)) {
        return EINVAL;
    }
    attr->clock_id = clock;
    return 0;
}

int pthread_cond_init(pthread_cond_t* condition,
                      const pthread_condattr_t* attr) {
    (void)attr;
    if (condition == NULL) return EINVAL;
    condition->sequence = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t* condition) {
    if (condition == NULL) return EINVAL;
    condition->sequence = 0;
    return 0;
}

int pthread_cond_wait(pthread_cond_t* condition, pthread_mutex_t* mutex) {
    uint32_t sequence;
    int result;
    if (condition == NULL || mutex == NULL) return EINVAL;
    sequence = __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE);
    result = pthread_mutex_unlock(mutex);
    if (result != 0) return result;
    __libc_os_wait_address(&condition->sequence, sequence);
    return pthread_mutex_lock(mutex);
}

int pthread_cond_timedwait(pthread_cond_t* condition, pthread_mutex_t* mutex,
                           const struct timespec* absolute_time) {
    uint32_t sequence;
    int result;
    if (condition == NULL || mutex == NULL || !valid_time(absolute_time)) {
        return EINVAL;
    }
    sequence = __atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE);
    result = pthread_mutex_unlock(mutex);
    if (result != 0) return result;
    while (__atomic_load_n(&condition->sequence, __ATOMIC_ACQUIRE) == sequence) {
        if (deadline_reached(absolute_time)) {
            pthread_mutex_lock(mutex);
            return ETIMEDOUT;
        }
        __libc_os_sleep(1);
    }
    return pthread_mutex_lock(mutex);
}

int pthread_cond_signal(pthread_cond_t* condition) {
    if (condition == NULL) return EINVAL;
    __atomic_add_fetch(&condition->sequence, 1, __ATOMIC_RELEASE);
    __libc_os_wake_address(&condition->sequence, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t* condition) {
    if (condition == NULL) return EINVAL;
    __atomic_add_fetch(&condition->sequence, 1, __ATOMIC_RELEASE);
    __libc_os_wake_address(&condition->sequence, UINT32_MAX);
    return 0;
}

int pthread_once(pthread_once_t* once, void (*initializer)(void)) {
    uint32_t expected = 0;
    if (once == NULL || initializer == NULL) return EINVAL;
    if (__atomic_compare_exchange_n(&once->state, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        initializer();
        __atomic_store_n(&once->state, 2, __ATOMIC_RELEASE);
        __libc_os_wake_address(&once->state, UINT32_MAX);
        return 0;
    }
    while (__atomic_load_n(&once->state, __ATOMIC_ACQUIRE) != 2) {
        __libc_os_wait_address(&once->state, 1);
    }
    return 0;
}

int pthread_key_create(pthread_key_t* key, void (*destructor)(void*)) {
    unsigned int index;
    if (key == NULL) return EINVAL;
    lock_registry();
    for (index = 0; index < PTHREAD_KEYS_MAX; ++index) {
        if (!keys[index].used) {
            keys[index].used = 1;
            keys[index].destructor = destructor;
            *key = index;
            unlock_registry();
            return 0;
        }
    }
    unlock_registry();
    return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
    size_t index;
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    lock_registry();
    if (!keys[key].used) {
        unlock_registry();
        return EINVAL;
    }
    keys[key].used = 0;
    keys[key].destructor = NULL;
    for (index = 0; index < LIBC_THREAD_CAPACITY; ++index) {
        specific_values[index].values[key] = NULL;
    }
    unlock_registry();
    return 0;
}

void* pthread_getspecific(pthread_key_t key) {
    void* result = NULL;
    if (key >= PTHREAD_KEYS_MAX) return NULL;
    lock_registry();
    if (keys[key].used) {
        ThreadSpecificValues* values = find_specific(pthread_self(), 0);
        if (values != NULL) result = values->values[key];
    }
    unlock_registry();
    return result;
}

int pthread_setspecific(pthread_key_t key, const void* value) {
    ThreadSpecificValues* values;
    if (key >= PTHREAD_KEYS_MAX) return EINVAL;
    lock_registry();
    if (!keys[key].used) {
        unlock_registry();
        return EINVAL;
    }
    values = find_specific(pthread_self(), 1);
    if (values == NULL) {
        unlock_registry();
        return ENOMEM;
    }
    values->values[key] = (void*)value;
    unlock_registry();
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t* lock,
                        const pthread_rwlockattr_t* attr) {
    (void)attr;
    if (lock == NULL) return EINVAL;
    memset(lock, 0, sizeof(*lock));
    pthread_mutex_init(&lock->mutex, NULL);
    pthread_cond_init(&lock->condition, NULL);
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t* lock) {
    if (lock == NULL) return EINVAL;
    pthread_mutex_lock(&lock->mutex);
    if (lock->writer || lock->readers || lock->waiting_writers) {
        pthread_mutex_unlock(&lock->mutex);
        return EBUSY;
    }
    pthread_mutex_unlock(&lock->mutex);
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t* lock) {
    if (lock == NULL) return EINVAL;
    pthread_mutex_lock(&lock->mutex);
    while (lock->writer || lock->waiting_writers) {
        pthread_cond_wait(&lock->condition, &lock->mutex);
    }
    ++lock->readers;
    pthread_mutex_unlock(&lock->mutex);
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t* lock) {
    int result = 0;
    if (lock == NULL) return EINVAL;
    pthread_mutex_lock(&lock->mutex);
    if (lock->writer || lock->waiting_writers) result = EBUSY;
    else ++lock->readers;
    pthread_mutex_unlock(&lock->mutex);
    return result;
}

int pthread_rwlock_wrlock(pthread_rwlock_t* lock) {
    if (lock == NULL) return EINVAL;
    pthread_mutex_lock(&lock->mutex);
    ++lock->waiting_writers;
    while (lock->writer || lock->readers) {
        pthread_cond_wait(&lock->condition, &lock->mutex);
    }
    --lock->waiting_writers;
    lock->writer = 1;
    pthread_mutex_unlock(&lock->mutex);
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t* lock) {
    int result = 0;
    if (lock == NULL) return EINVAL;
    pthread_mutex_lock(&lock->mutex);
    if (lock->writer || lock->readers) result = EBUSY;
    else lock->writer = 1;
    pthread_mutex_unlock(&lock->mutex);
    return result;
}

int pthread_rwlock_unlock(pthread_rwlock_t* lock) {
    if (lock == NULL) return EINVAL;
    pthread_mutex_lock(&lock->mutex);
    if (lock->writer) lock->writer = 0;
    else if (lock->readers) --lock->readers;
    else {
        pthread_mutex_unlock(&lock->mutex);
        return EPERM;
    }
    pthread_cond_broadcast(&lock->condition);
    pthread_mutex_unlock(&lock->mutex);
    return 0;
}

int pthread_barrier_init(pthread_barrier_t* barrier,
                         const pthread_barrierattr_t* attr,
                         unsigned int count) {
    (void)attr;
    if (barrier == NULL || count == 0) return EINVAL;
    memset(barrier, 0, sizeof(*barrier));
    pthread_mutex_init(&barrier->mutex, NULL);
    pthread_cond_init(&barrier->condition, NULL);
    barrier->threshold = count;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t* barrier) {
    if (barrier == NULL) return EINVAL;
    return barrier->waiting == 0 ? 0 : EBUSY;
}

int pthread_barrier_wait(pthread_barrier_t* barrier) {
    uint32_t generation;
    if (barrier == NULL || barrier->threshold == 0) return EINVAL;
    pthread_mutex_lock(&barrier->mutex);
    generation = barrier->generation;
    if (++barrier->waiting == barrier->threshold) {
        barrier->waiting = 0;
        ++barrier->generation;
        pthread_cond_broadcast(&barrier->condition);
        pthread_mutex_unlock(&barrier->mutex);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (generation == barrier->generation) {
        pthread_cond_wait(&barrier->condition, &barrier->mutex);
    }
    pthread_mutex_unlock(&barrier->mutex);
    return 0;
}

int pthread_spin_init(pthread_spinlock_t* lock, int task_shared) {
    if (lock == NULL) return EINVAL;
    if (task_shared != PTHREAD_PROCESS_PRIVATE) return ENOTSUP;
    *lock = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t* lock) {
    if (lock == NULL) return EINVAL;
    return __atomic_load_n(lock, __ATOMIC_ACQUIRE) == 0 ? 0 : EBUSY;
}

int pthread_spin_lock(pthread_spinlock_t* lock) {
    if (lock == NULL) return EINVAL;
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) __libc_os_yield();
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t* lock) {
    if (lock == NULL) return EINVAL;
    return __atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE) ? EBUSY : 0;
}

int pthread_spin_unlock(pthread_spinlock_t* lock) {
    if (lock == NULL) return EINVAL;
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    return 0;
}

int pthread_cancel(pthread_t thread) {
    (void)thread;
    return ENOSYS;
}

int pthread_setcancelstate(int state, int* old_state) {
    (void)state;
    (void)old_state;
    return ENOSYS;
}

int pthread_setcanceltype(int type, int* old_type) {
    (void)type;
    (void)old_type;
    return ENOSYS;
}

void pthread_testcancel(void) {}
