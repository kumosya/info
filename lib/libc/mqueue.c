#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdarg.h>
#include <stdint.h>

#include "os_adapter.h"

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

mqd_t mq_open(const char* name, int flags, ...) {
    uint32_t maximum_messages = 16;
    uint32_t message_size = 256;
    if (name == NULL || (flags & ~(O_ACCMODE | O_CREAT | O_EXCL |
                                   O_NONBLOCK)) != 0) {
        errno = EINVAL;
        return (mqd_t)-1;
    }
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        const struct mq_attr* attributes;
        va_start(arguments, flags);
        (void)va_arg(arguments, mode_t);
        attributes = va_arg(arguments, const struct mq_attr*);
        va_end(arguments);
        if (attributes != NULL) {
            if (attributes->mq_maxmsg <= 0 || attributes->mq_maxmsg > 16 ||
                attributes->mq_msgsize <= 0 ||
                attributes->mq_msgsize > 256) {
                errno = EINVAL;
                return (mqd_t)-1;
            }
            maximum_messages = (uint32_t)attributes->mq_maxmsg;
            message_size = (uint32_t)attributes->mq_msgsize;
        }
    }
    {
        uint64_t descriptor = __libc_os_open_message_queue(
            name, flags, maximum_messages, message_size);
        if ((int64_t)descriptor < 0 || descriptor == 0) {
            errno = (flags & O_EXCL) ? EEXIST : ENOENT;
            return (mqd_t)-1;
        }
        return descriptor;
    }
}

int mq_close(mqd_t descriptor) {
    if (!__libc_os_close_handle(descriptor)) {
        errno = EBADF;
        return -1;
    }
    return 0;
}

int mq_unlink(const char* name) {
    if (name == NULL || __libc_os_unlink_message_queue(name) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int mq_send(mqd_t descriptor, const char* message, size_t length,
            unsigned int priority) {
    int result;
    if ((message == NULL && length != 0) || priority >= MQ_PRIO_MAX) {
        errno = EINVAL;
        return -1;
    }
    result = __libc_os_send_message_queue(descriptor, message, length, priority);
    if (result != 0) {
        errno = result == -6 ? EAGAIN : EINVAL;
        return -1;
    }
    return 0;
}

ssize_t mq_receive(mqd_t descriptor, char* message, size_t length,
                   unsigned int* priority) {
    uint32_t value = 0;
    int64_t result;
    if (message == NULL) {
        errno = EINVAL;
        return -1;
    }
    result = __libc_os_receive_message_queue(descriptor, message, length, &value);
    if (result < 0) {
        errno = result == -6 ? EAGAIN : EMSGSIZE;
        return -1;
    }
    if (priority != NULL) *priority = value;
    return (ssize_t)result;
}

int mq_getattr(mqd_t descriptor, struct mq_attr* attributes) {
    OsMessageQueueAttributes value;
    if (attributes == NULL ||
        __libc_os_get_message_queue_attributes(descriptor, &value) != 0) {
        errno = EINVAL;
        return -1;
    }
    attributes->mq_flags = (long)value.flags;
    attributes->mq_maxmsg = (long)value.maximumMessages;
    attributes->mq_msgsize = (long)value.messageSize;
    attributes->mq_curmsgs = (long)value.currentMessages;
    return 0;
}

int mq_setattr(mqd_t descriptor, const struct mq_attr* attributes,
               struct mq_attr* old_attributes) {
    uint64_t old_flags;
    if (attributes == NULL || (attributes->mq_flags & ~O_NONBLOCK) != 0 ||
        __libc_os_set_message_queue_flags(descriptor,
                                   (uint64_t)attributes->mq_flags,
                                   &old_flags) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (old_attributes != NULL) {
        if (mq_getattr(descriptor, old_attributes) != 0) return -1;
        old_attributes->mq_flags = (long)old_flags;
    }
    return 0;
}

int mq_timedsend(mqd_t descriptor, const char* message, size_t length,
                 unsigned int priority,
                 const struct timespec* absolute_time) {
    struct mq_attr attributes;
    struct mq_attr nonblocking;
    if (!valid_time(absolute_time) || mq_getattr(descriptor, &attributes) != 0) {
        errno = EINVAL;
        return -1;
    }
    nonblocking = attributes;
    nonblocking.mq_flags |= O_NONBLOCK;
    if (mq_setattr(descriptor, &nonblocking, NULL) != 0) return -1;
    while (mq_send(descriptor, message, length, priority) != 0) {
        if (errno != EAGAIN || deadline_reached(absolute_time)) {
            (void)mq_setattr(descriptor, &attributes, NULL);
            if (errno == EAGAIN) errno = ETIMEDOUT;
            return -1;
        }
        __libc_os_sleep(1);
    }
    (void)mq_setattr(descriptor, &attributes, NULL);
    return 0;
}

ssize_t mq_timedreceive(mqd_t descriptor, char* message, size_t length,
                        unsigned int* priority,
                        const struct timespec* absolute_time) {
    struct mq_attr attributes;
    struct mq_attr nonblocking;
    ssize_t result;
    if (!valid_time(absolute_time) || mq_getattr(descriptor, &attributes) != 0) {
        errno = EINVAL;
        return -1;
    }
    nonblocking = attributes;
    nonblocking.mq_flags |= O_NONBLOCK;
    if (mq_setattr(descriptor, &nonblocking, NULL) != 0) return -1;
    while ((result = mq_receive(descriptor, message, length, priority)) < 0) {
        if (errno != EAGAIN || deadline_reached(absolute_time)) {
            (void)mq_setattr(descriptor, &attributes, NULL);
            if (errno == EAGAIN) errno = ETIMEDOUT;
            return -1;
        }
        __libc_os_sleep(1);
    }
    (void)mq_setattr(descriptor, &attributes, NULL);
    return result;
}

int mq_notify(mqd_t descriptor, const struct sigevent* notification) {
    int result;
    if (notification != NULL && notification->sigev_notify == SIGEV_THREAD) {
        errno = ENOTSUP;
        return -1;
    }
    if (notification != NULL && notification->sigev_notify != SIGEV_NONE &&
        notification->sigev_notify != SIGEV_SIGNAL) {
        errno = EINVAL;
        return -1;
    }
    result = __libc_os_register_message_queue_notification(
        descriptor,
        notification == NULL
            ? 0
            : (notification->sigev_notify == SIGEV_SIGNAL ? 2 : 1),
        notification != NULL && notification->sigev_notify == SIGEV_SIGNAL
            ? (uint32_t)notification->sigev_signo
            : 0);
    if (result != 0) {
        errno = result == -8 ? EBUSY : EINVAL;
        return -1;
    }
    return 0;
}
