#include <errno.h>
#include <pthread.h>

/* 读写锁的状态仅在内部 mutex 下改变，超时也在此锁下撤销等待计数。 */
static int timed_lock(pthread_rwlock_t* lock, clockid_t clock,
                       const struct timespec* limit, int write)
{
    if (!lock || (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)) return EINVAL;
    int error = pthread_mutex_clocklock(&lock->mutex, clock, limit);
    if (error) return error;

    if (write) ++lock->waiting_writers;
    while (lock->writer || (write ? lock->readers : lock->waiting_writers))
    {
        error = pthread_cond_clockwait(&lock->condition, &lock->mutex, clock, limit);
        if (error) break;
    }

    if (write) --lock->waiting_writers;
    if (!error)
    {
        if (write) lock->writer = 1;
        else if (lock->readers == UINT32_MAX) error = EAGAIN;
        else ++lock->readers;
    }
    /* 最后一名等待写者超时后，读者不能继续被过期的写者阻挡。 */
    if (error && write && !lock->waiting_writers) pthread_cond_broadcast(&lock->condition);
    pthread_mutex_unlock(&lock->mutex);
    return error;
}

int pthread_rwlock_clockrdlock(pthread_rwlock_t* restrict lock, clockid_t clock,
                                const struct timespec* restrict limit)
{
    return timed_lock(lock, clock, limit, 0);
}

int pthread_rwlock_clockwrlock(pthread_rwlock_t* restrict lock, clockid_t clock,
                                const struct timespec* restrict limit)
{
    return timed_lock(lock, clock, limit, 1);
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t* restrict lock,
                                const struct timespec* restrict limit)
{
    return timed_lock(lock, CLOCK_REALTIME, limit, 0);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t* restrict lock,
                                const struct timespec* restrict limit)
{
    return timed_lock(lock, CLOCK_REALTIME, limit, 1);
}
