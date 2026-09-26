#pragma once

#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

struct sigevent;

struct aiocb {
    int aio_fildes;
    off_t aio_offset;
    volatile void* aio_buf;
    size_t aio_nbytes;
    int aio_reqprio;
    struct sigevent* aio_sigevent;
    int aio_lio_opcode;
    uint64_t _endpoint;
    pthread_t _worker;
    volatile uint32_t _state;
    int _error;
    ssize_t _return_value;
    int _operation;
};

#define AIO_CANCELED 0
#define AIO_NOTCANCELED 1
#define AIO_ALLDONE 2

#define LIO_READ 0
#define LIO_WRITE 1
#define LIO_NOP 2
#define LIO_WAIT 0
#define LIO_NOWAIT 1

#ifdef __cplusplus
extern "C" {
#endif

int aio_read(struct aiocb* control);
int aio_write(struct aiocb* control);
int aio_error(const struct aiocb* control);
ssize_t aio_return(struct aiocb* control);
int aio_cancel(int file_descriptor, struct aiocb* control);
int aio_suspend(const struct aiocb* const controls[], int count,
                const struct timespec* timeout);
int aio_fsync(int operation, struct aiocb* control);
int lio_listio(int mode, struct aiocb* const controls[], int count,
               struct sigevent* notification);

#ifdef __cplusplus
}
#endif
