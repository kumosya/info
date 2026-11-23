#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(expression) do { if (!(expression)) { \
    printf("B10 FAIL line=%d errno=%d\n", __LINE__, errno); return __LINE__; } } while (0)

static struct timespec after(clockid_t clock, long nanoseconds)
{
    struct timespec result;
    clock_gettime(clock, &result);
    result.tv_nsec += nanoseconds;
    if (result.tv_nsec >= 1000000000L)
    {
        ++result.tv_sec;
        result.tv_nsec -= 1000000000L;
    }
    return result;
}

static void* stack_entry(void* value)
{
    volatile char byte = 1;
    uintptr_t address = (uintptr_t)&byte;
    uintptr_t begin = (uintptr_t)value;
    return (void*)(uintptr_t)(address >= begin && address < begin + 65536);
}

static volatile sig_atomic_t signal_seen;
static void receive_signal(int number) { signal_seen = number; }
static void* send_signal(void* target)
{
    usleep(100000);
    pthread_kill((pthread_t)(uintptr_t)target, SIGUSR1);
    return NULL;
}

struct Shared
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    sem_t ready;
    unsigned value;
};

int main(void)
{
    pthread_attr_t attr;
    size_t guard, size;
    int scope;
    void* address;
    CHECK(pthread_attr_init(&attr) == 0);
    CHECK(pthread_attr_getguardsize(&attr, &guard) == 0 && guard == 4096);
    CHECK(pthread_attr_setguardsize(&attr, 17) == 0);
    CHECK(pthread_attr_getguardsize(&attr, &guard) == 0 && guard == 17);
    CHECK(pthread_attr_getscope(&attr, &scope) == 0 && scope == PTHREAD_SCOPE_SYSTEM);
    CHECK(pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS) == ENOTSUP);
    void* stack = mmap(NULL, 65536, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(stack != MAP_FAILED && pthread_attr_setstack(&attr, stack, 65536) == 0);
    CHECK(pthread_attr_getstack(&attr, &address, &size) == 0 && address == stack && size == 65536);
    pthread_t thread;
    void* result;
    CHECK(pthread_create(&thread, &attr, stack_entry, stack) == 0);
    CHECK(pthread_join(thread, &result) == 0 && (uintptr_t)result == 1);
    *(volatile char*)stack = 42;
    CHECK(munmap(stack, 65536) == 0);

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
    CHECK(pthread_mutex_lock(&mutex) == 0);
    struct timespec limit = after(CLOCK_MONOTONIC, 20000000);
    CHECK(pthread_cond_clockwait(&condition, &mutex, CLOCK_MONOTONIC, &limit) == ETIMEDOUT);
    CHECK(pthread_mutex_unlock(&mutex) == 0);
    limit.tv_nsec = 1000000000L;
    CHECK(pthread_cond_clockwait(&condition, &mutex, CLOCK_MONOTONIC, &limit) == EINVAL);

    sem_t semaphore;
    CHECK(sem_init(&semaphore, 0, 1) == 0);
    limit = after(CLOCK_MONOTONIC, 20000000);
    CHECK(sem_clockwait(&semaphore, CLOCK_MONOTONIC, &limit) == 0);
    CHECK(sem_clockwait(&semaphore, CLOCK_MONOTONIC, &limit) == -1 && errno == ETIMEDOUT);
    CHECK(sem_post(&semaphore) == 0 && sem_trywait(&semaphore) == 0);
    struct sigaction action = {0}, previous;
    action.sa_handler = receive_signal;
    CHECK(sigaction(SIGUSR1, &action, &previous) == 0);
    CHECK(pthread_create(&thread, NULL, send_signal, (void*)(uintptr_t)pthread_self()) == 0);
    limit = after(CLOCK_MONOTONIC, 0);
    limit.tv_sec += 5;
    int interrupted = sem_clockwait(&semaphore, CLOCK_MONOTONIC, &limit);
    int interrupted_error = errno;
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(interrupted == -1 && interrupted_error == EINTR && signal_seen == SIGUSR1);
    /* 信号先到、再临时解除屏蔽：不能漏睡，也不能把临时掩码留给调用者。 */
    extern int test_wait_address_masked(volatile uint32_t*, uint32_t,
                                        uint64_t, const uint64_t*);
    sigset_t blocked, original, current;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR1);
    CHECK(pthread_sigmask(SIG_BLOCK, &blocked, &original) == 0);
    signal_seen = 0;
    CHECK(pthread_kill(pthread_self(), SIGUSR1) == 0 && signal_seen == 0);
    uint64_t temporary_mask = 0;
    volatile uint32_t sequence = 0;
    limit = after(CLOCK_MONOTONIC, 0);
    uint64_t deadline = (uint64_t)(limit.tv_sec + 1) * 1000000000ULL + limit.tv_nsec;
    CHECK(test_wait_address_masked(&sequence, 0, deadline, &temporary_mask) == -10);
    CHECK(signal_seen == SIGUSR1);
    CHECK(pthread_sigmask(SIG_SETMASK, NULL, &current) == 0);
    CHECK(sigismember(&current, SIGUSR1) == 1);

    /* 没有信号时也要恢复旧掩码，包括期望值不匹配的立即返回路径。 */
    CHECK(test_wait_address_masked(&sequence, 1, deadline, &temporary_mask) == -6);
    CHECK(pthread_sigmask(SIG_SETMASK, NULL, &current) == 0);
    CHECK(sigismember(&current, SIGUSR1) == 1);
    CHECK(pthread_sigmask(SIG_SETMASK, &original, NULL) == 0);
    CHECK(sigaction(SIGUSR1, &previous, NULL) == 0);

    pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;
    CHECK(pthread_rwlock_rdlock(&rwlock) == 0);
    limit = after(CLOCK_MONOTONIC, 20000000);
    CHECK(pthread_rwlock_clockwrlock(&rwlock, CLOCK_MONOTONIC, &limit) == ETIMEDOUT);
    CHECK(rwlock.waiting_writers == 0 && pthread_rwlock_unlock(&rwlock) == 0);
    CHECK(pthread_rwlock_trywrlock(&rwlock) == 0 && pthread_rwlock_unlock(&rwlock) == 0);

    char name[64];
    snprintf(name, sizeof(name), "/b10-sync-%d", (int)getpid());
    int descriptor = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(descriptor >= 0 && ftruncate(descriptor, 4096) == 0);
    struct Shared* shared = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, descriptor, 0);
    CHECK(shared != MAP_FAILED);
    struct Shared* alias = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, descriptor, 0);
    CHECK(alias != MAP_FAILED && alias != shared);
    CHECK(close(descriptor) == 0 && shm_unlink(name) == 0);
    pthread_mutexattr_t ma;
    pthread_condattr_t ca;
    CHECK(pthread_mutexattr_init(&ma) == 0 && pthread_condattr_init(&ca) == 0);
    CHECK(pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED) == 0);
    CHECK(pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED) == 0);
    CHECK(pthread_mutex_init(&shared->mutex, &ma) == 0);
    CHECK(pthread_cond_init(&shared->condition, &ca) == 0 && sem_init(&shared->ready, 1, 0) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child)
    {
        pthread_mutex_lock(&alias->mutex);
        alias->value = 123;
        pthread_cond_signal(&alias->condition);
        pthread_mutex_unlock(&alias->mutex);
        sem_post(&alias->ready);
        _exit(0);
    }
    CHECK(pthread_mutex_lock(&shared->mutex) == 0);
    limit = after(CLOCK_REALTIME, 500000000);
    while (!shared->value)
    {
        CHECK(pthread_cond_timedwait(&shared->condition, &shared->mutex, &limit) == 0);
    }
    CHECK(shared->value == 123 && pthread_mutex_unlock(&shared->mutex) == 0);
    CHECK(sem_clockwait(&shared->ready, CLOCK_REALTIME, &limit) == 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(munmap(shared, 4096) == 0 && munmap(alias, 4096) == 0);
    puts("B10 SYNC PASS");
    return 0;
}
