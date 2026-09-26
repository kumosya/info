#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "internal.h"

#define AIO_STATE_IDLE 0
#define AIO_STATE_PROGRESS 1
#define AIO_STATE_DONE 2
#define AIO_STATE_RETURNED 3
#define AIO_OPERATION_READ 1
#define AIO_OPERATION_WRITE 2
#define AIO_MAXIMUM_TRANSFER 4088U

static void* aio_worker(void* argument) {
    struct aiocb* control = (struct aiocb*)argument;
    OsFileOperationResult result;
    void* buffer = control->_operation == AIO_OPERATION_READ
                       ? (void*)control->aio_buf
                       : NULL;
    size_t capacity = control->_operation == AIO_OPERATION_READ
                          ? control->aio_nbytes
                          : 0;
    if (os_wait_file_operation(control->_endpoint, &result, buffer, capacity)) {
        control->_error = result.status == 0 ? 0 : EIO;
        control->_return_value = result.status == 0
                                     ? (ssize_t)result.transferred
                                     : -1;
    } else {
        control->_error = EIO;
        control->_return_value = -1;
    }
    os_close_handle(control->_endpoint);
    control->_endpoint = 0;
    __atomic_store_n(&control->_state, AIO_STATE_DONE, __ATOMIC_RELEASE);
    os_wake_address(&control->_state, UINT32_MAX);
    return NULL;
}

static int start_operation(struct aiocb* control, int operation) {
    OpenDescription* description;
    int sent;
    int result;
    if (control == NULL || control->aio_buf == NULL ||
        control->aio_nbytes == 0 ||
        control->aio_nbytes > AIO_MAXIMUM_TRANSFER ||
        control->aio_offset < 0 || control->aio_reqprio != 0) {
        errno = EINVAL;
        return -1;
    }
    description = libcDescriptor(control->aio_fildes);
    if (description == NULL) {
        errno = EBADF;
        return -1;
    }
    if (description->kind != DESCRIPTOR_FILE) {
        errno = ESPIPE;
        return -1;
    }
    if (__atomic_load_n(&control->_state, __ATOMIC_ACQUIRE) ==
        AIO_STATE_PROGRESS) {
        errno = EBUSY;
        return -1;
    }
    control->_endpoint = os_create_endpoint();
    if (control->_endpoint == 0) {
        errno = EAGAIN;
        return -1;
    }
    control->_worker = 0;
    control->_error = EINPROGRESS;
    control->_return_value = -1;
    control->_operation = operation;
    __atomic_store_n(&control->_state, AIO_STATE_PROGRESS, __ATOMIC_RELEASE);
    if (operation == AIO_OPERATION_READ) {
        sent = os_read_file_async(libcFsService(), description->handle,
                                  (uint64_t)control->aio_offset,
                                  control->aio_nbytes, control->_endpoint,
                                  (uint64_t)(uintptr_t)control);
    } else {
        sent = os_write_file_async(libcFsService(), description->handle,
                                   (uint64_t)control->aio_offset,
                                   (const void*)control->aio_buf,
                                   control->aio_nbytes, control->_endpoint,
                                   (uint64_t)(uintptr_t)control);
    }
    if (!sent) {
        os_close_handle(control->_endpoint);
        control->_endpoint = 0;
        control->_state = AIO_STATE_IDLE;
        errno = EIO;
        return -1;
    }
    result = pthread_create(&control->_worker, NULL, aio_worker, control);
    if (result != 0) {
        aio_worker(control);
    }
    return 0;
}

int aio_read(struct aiocb* control) {
    return start_operation(control, AIO_OPERATION_READ);
}

int aio_write(struct aiocb* control) {
    return start_operation(control, AIO_OPERATION_WRITE);
}

int aio_error(const struct aiocb* control) {
    uint32_t state;
    if (control == NULL) return EINVAL;
    state = __atomic_load_n(&control->_state, __ATOMIC_ACQUIRE);
    if (state == AIO_STATE_PROGRESS) return EINPROGRESS;
    if (state != AIO_STATE_DONE && state != AIO_STATE_RETURNED) return EINVAL;
    return control->_error;
}

ssize_t aio_return(struct aiocb* control) {
    uint32_t expected = AIO_STATE_DONE;
    ssize_t result;
    if (control == NULL ||
        !__atomic_compare_exchange_n(&control->_state, &expected,
                                     AIO_STATE_RETURNED, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        errno = expected == AIO_STATE_PROGRESS ? EINPROGRESS : EINVAL;
        return -1;
    }
    if (control->_worker != 0) {
        (void)pthread_join(control->_worker, NULL);
        control->_worker = 0;
    }
    result = control->_return_value;
    if (result < 0) errno = control->_error;
    return result;
}

int aio_cancel(int file_descriptor, struct aiocb* control) {
    (void)file_descriptor;
    if (control == NULL) return AIO_NOTCANCELED;
    return __atomic_load_n(&control->_state, __ATOMIC_ACQUIRE) ==
                   AIO_STATE_PROGRESS
               ? AIO_NOTCANCELED
               : AIO_ALLDONE;
}

int aio_suspend(const struct aiocb* const controls[], int count,
                const struct timespec* timeout) {
    uint64_t deadline = 0;
    int index;
    if (controls == NULL || count <= 0 ||
        (timeout != NULL &&
         (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
          timeout->tv_nsec >= 1000000000L))) {
        errno = EINVAL;
        return -1;
    }
    if (timeout != NULL) {
        uint64_t duration = (uint64_t)timeout->tv_sec * 1000000000ULL +
                            (uint64_t)timeout->tv_nsec;
        deadline = os_get_monotonic_time() + duration;
    }
    while (1) {
        for (index = 0; index < count; ++index) {
            if (controls[index] != NULL &&
                aio_error(controls[index]) != EINPROGRESS) return 0;
        }
        if (timeout != NULL && os_get_monotonic_time() >= deadline) {
            errno = EAGAIN;
            return -1;
        }
        os_sleep(1);
    }
}

int aio_fsync(int operation, struct aiocb* control) {
    if (control == NULL || (operation != O_SYNC && operation != O_DSYNC)) {
        errno = EINVAL;
        return -1;
    }
    control->_endpoint = 0;
    control->_worker = 0;
    control->_operation = 0;
    control->_return_value = fsync(control->aio_fildes);
    control->_error = control->_return_value == 0 ? 0 : errno;
    control->_state = AIO_STATE_DONE;
    return 0;
}

int lio_listio(int mode, struct aiocb* const controls[], int count,
               struct sigevent* notification) {
    int index;
    int failed = 0;
    (void)notification;
    if ((mode != LIO_WAIT && mode != LIO_NOWAIT) || controls == NULL ||
        count < 0) {
        errno = EINVAL;
        return -1;
    }
    for (index = 0; index < count; ++index) {
        if (controls[index] == NULL ||
            controls[index]->aio_lio_opcode == LIO_NOP) continue;
        if ((controls[index]->aio_lio_opcode == LIO_READ
                 ? aio_read(controls[index])
                 : controls[index]->aio_lio_opcode == LIO_WRITE
                       ? aio_write(controls[index])
                       : -1) != 0) failed = 1;
    }
    if (mode == LIO_WAIT) {
        for (index = 0; index < count; ++index) {
            const struct aiocb* one[1];
            if (controls[index] == NULL ||
                controls[index]->aio_lio_opcode == LIO_NOP) continue;
            one[0] = controls[index];
            if (aio_suspend(one, 1, NULL) != 0) failed = 1;
        }
    }
    if (failed) {
        errno = EIO;
        return -1;
    }
    return 0;
}
