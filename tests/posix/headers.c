#include <aio.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <threads.h>
#include <unistd.h>

#if _POSIX_VERSION != 202405L
#error "Unexpected POSIX version"
#endif

static void checkFunctionTypes(void) {
    ssize_t (*readFunction)(int, void*, size_t) = read;
    ssize_t (*writeFunction)(int, const void*, size_t) = write;
    int (*clockFunction)(clockid_t, struct timespec*) = clock_gettime;
    pid_t (*waitFunction)(pid_t, int*, int) = waitpid;
    int (*threadFunction)(pthread_t*, const pthread_attr_t*,
                          void* (*)(void*), void*) = pthread_create;
    int (*semaphoreFunction)(sem_t*) = sem_wait;
    sem_t* (*namedSemaphoreFunction)(const char*, int, ...) = sem_open;
    void* (*mappingFunction)(void*, size_t, int, int, int, off_t) = mmap;
    int (*sharedMemoryFunction)(const char*, int, mode_t) = shm_open;
    int (*threadSignalFunction)(pthread_t, int) = pthread_kill;
    int (*aioFunction)(struct aiocb*) = aio_read;
    mqd_t (*messageQueueFunction)(const char*, int, ...) = mq_open;
    (void)readFunction;
    (void)writeFunction;
    (void)clockFunction;
    (void)waitFunction;
    (void)threadFunction;
    (void)semaphoreFunction;
    (void)namedSemaphoreFunction;
    (void)mappingFunction;
    (void)sharedMemoryFunction;
    (void)threadSignalFunction;
    (void)aioFunction;
    (void)messageQueueFunction;
}

int main(void) {
    checkFunctionTypes();
    return _POSIX_VERSION == 202405L && PATH_MAX >= 256 && PIPE_BUF >= 512
               ? 0
               : 1;
}
