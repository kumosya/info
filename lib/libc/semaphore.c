#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include "os/c_api.h"

#define NAMED_SEMAPHORE_CAPACITY 16
#define NAMED_SEMAPHORE_PAGE_SIZE 4096
#define NAMED_SEMAPHORE_PREFIX "/__posix_sem__"

typedef struct {
    sem_t* semaphore;
    int descriptor;
} NamedSemaphore;

static NamedSemaphore named_semaphores[NAMED_SEMAPHORE_CAPACITY];
static volatile uint8_t named_semaphore_lock;

static void lock_named_semaphores(void) {
    while (__atomic_test_and_set(&named_semaphore_lock, __ATOMIC_ACQUIRE)) {
        os_yield();
    }
}

static void unlock_named_semaphores(void) {
    __atomic_clear(&named_semaphore_lock, __ATOMIC_RELEASE);
}

static int make_semaphore_name(char destination[64], const char* name) {
    static const char prefix[] = NAMED_SEMAPHORE_PREFIX;
    size_t output = 0;
    size_t input = 1;
    if (name == NULL || name[0] != '/' || name[1] == '\0') return 0;
    while (prefix[output] != '\0') {
        destination[output] = prefix[output];
        ++output;
    }
    while (name[input] != '\0') {
        if (name[input] == '/' || output + 1 >= 64) return 0;
        destination[output++] = name[input++];
    }
    destination[output] = '\0';
    return 1;
}

static int register_named_semaphore(sem_t* semaphore, int descriptor) {
    int index;
    lock_named_semaphores();
    for (index = 0; index < NAMED_SEMAPHORE_CAPACITY; ++index) {
        if (named_semaphores[index].semaphore != NULL) continue;
        named_semaphores[index].semaphore = semaphore;
        named_semaphores[index].descriptor = descriptor;
        unlock_named_semaphores();
        return 1;
    }
    unlock_named_semaphores();
    return 0;
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

int sem_init(sem_t* semaphore, int process_shared, unsigned int value) {
    if (semaphore == NULL || value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }
    (void)process_shared;
    semaphore->value = value;
    semaphore->initialized = 1;
    return 0;
}

int sem_destroy(sem_t* semaphore) {
    if (semaphore == NULL || !semaphore->initialized) {
        errno = EINVAL;
        return -1;
    }
    semaphore->initialized = 0;
    return 0;
}

int sem_trywait(sem_t* semaphore) {
    uint32_t value;
    if (semaphore == NULL || !semaphore->initialized) {
        errno = EINVAL;
        return -1;
    }
    value = __atomic_load_n(&semaphore->value, __ATOMIC_ACQUIRE);
    while (value != 0) {
        if (__atomic_compare_exchange_n(&semaphore->value, &value, value - 1,
                                        0, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED)) return 0;
    }
    errno = EAGAIN;
    return -1;
}

int sem_wait(sem_t* semaphore) {
    if (semaphore == NULL || !semaphore->initialized) {
        errno = EINVAL;
        return -1;
    }
    while (sem_trywait(semaphore) != 0) {
        if (errno != EAGAIN) return -1;
        os_wait_address(&semaphore->value, 0);
    }
    return 0;
}

int sem_timedwait(sem_t* semaphore, const struct timespec* absolute_time) {
    if (!valid_time(absolute_time)) {
        errno = EINVAL;
        return -1;
    }
    while (sem_trywait(semaphore) != 0) {
        if (errno != EAGAIN) return -1;
        if (deadline_reached(absolute_time)) {
            errno = ETIMEDOUT;
            return -1;
        }
        os_sleep(1);
    }
    return 0;
}

int sem_post(sem_t* semaphore) {
    uint32_t value;
    if (semaphore == NULL || !semaphore->initialized) {
        errno = EINVAL;
        return -1;
    }
    value = __atomic_load_n(&semaphore->value, __ATOMIC_RELAXED);
    do {
        if (value == SEM_VALUE_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
    } while (!__atomic_compare_exchange_n(&semaphore->value, &value, value + 1,
                                           0, __ATOMIC_RELEASE,
                                           __ATOMIC_RELAXED));
    os_wake_address(&semaphore->value, 1);
    return 0;
}

int sem_getvalue(sem_t* semaphore, int* value) {
    if (semaphore == NULL || value == NULL || !semaphore->initialized) {
        errno = EINVAL;
        return -1;
    }
    *value = (int)__atomic_load_n(&semaphore->value, __ATOMIC_ACQUIRE);
    return 0;
}

sem_t* sem_open(const char* name, int flags, ...) {
    char shared_name[64];
    mode_t mode = 0;
    unsigned int value = 0;
    int descriptor = -1;
    int created = 0;
    sem_t* semaphore;
    if (!make_semaphore_name(shared_name, name) ||
        (flags & ~(O_CREAT | O_EXCL)) != 0) {
        errno = EINVAL;
        return SEM_FAILED;
    }
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, mode_t);
        value = va_arg(arguments, unsigned int);
        va_end(arguments);
        if (value > SEM_VALUE_MAX) {
            errno = EINVAL;
            return SEM_FAILED;
        }
        descriptor = shm_open(shared_name, O_RDWR | O_CREAT | O_EXCL, mode);
        if (descriptor >= 0) {
            created = 1;
            if (ftruncate(descriptor, NAMED_SEMAPHORE_PAGE_SIZE) != 0) {
                close(descriptor);
                shm_unlink(shared_name);
                return SEM_FAILED;
            }
        } else if ((flags & O_EXCL) != 0 || errno != EEXIST) {
            return SEM_FAILED;
        }
    }
    if (descriptor < 0) descriptor = shm_open(shared_name, O_RDWR, mode);
    if (descriptor < 0) return SEM_FAILED;
    semaphore = mmap(NULL, NAMED_SEMAPHORE_PAGE_SIZE,
                     PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    if (semaphore == MAP_FAILED) {
        close(descriptor);
        if (created) shm_unlink(shared_name);
        return SEM_FAILED;
    }
    if (created) {
        semaphore->value = value;
        __atomic_store_n(&semaphore->initialized, 1, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&semaphore->initialized, __ATOMIC_ACQUIRE) == 0) {
            os_yield();
        }
    }
    if (!register_named_semaphore(semaphore, descriptor)) {
        munmap(semaphore, NAMED_SEMAPHORE_PAGE_SIZE);
        close(descriptor);
        errno = EMFILE;
        return SEM_FAILED;
    }
    return semaphore;
}

int sem_close(sem_t* semaphore) {
    int descriptor = -1;
    int index;
    lock_named_semaphores();
    for (index = 0; index < NAMED_SEMAPHORE_CAPACITY; ++index) {
        if (named_semaphores[index].semaphore != semaphore) continue;
        descriptor = named_semaphores[index].descriptor;
        named_semaphores[index] = (NamedSemaphore){0};
        break;
    }
    unlock_named_semaphores();
    if (descriptor < 0) {
        errno = EINVAL;
        return -1;
    }
    if (munmap(semaphore, NAMED_SEMAPHORE_PAGE_SIZE) != 0 ||
        close(descriptor) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int sem_unlink(const char* name) {
    char shared_name[64];
    if (!make_semaphore_name(shared_name, name)) {
        errno = EINVAL;
        return -1;
    }
    return shm_unlink(shared_name);
}
