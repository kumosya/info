/*
 * 本地描述符共用内核变化序号：先取快照、复查状态，再按同一序号登记睡眠。
 * 文件、管道及终端均只查询状态，等待过程不消费数据。
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <time.h>

#include "internal.h"

__attribute__((visibility("hidden"))) int __libc_poll_wait(
    struct pollfd* descriptors, nfds_t count,
    const struct timespec* restrict timeout, const sigset_t* restrict mask,
    int selecting) {
    const int saved_errno = errno;
    if (count > LIBC_DESCRIPTOR_CAPACITY) {
        errno = EINVAL;
        return -1;
    }
    if ((count &&
         !__libc_os_accessible(descriptors, count * sizeof(*descriptors), 1)) ||
        (timeout && !__libc_os_accessible(timeout, sizeof(*timeout), 0)) ||
        (mask && !__libc_os_accessible(mask, sizeof(*mask), 0))) {
        errno = EFAULT;
        return -1;
    }
    uint64_t deadline = 0;
    if (timeout) {
        if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
            timeout->tv_nsec >= 1000000000L ||
            (uint64_t)timeout->tv_sec >
                (UINT64_MAX - (uint64_t)timeout->tv_nsec) / 1000000000ULL) {
            errno = EINVAL;
            return -1;
        }
        const uint64_t duration =
            (uint64_t)timeout->tv_sec * 1000000000ULL + timeout->tv_nsec;
        const uint64_t now = __libc_os_get_monotonic_time();
        deadline =
            duration >= UINT64_MAX - now ? UINT64_MAX - 1 : now + duration;
        if (!deadline) deadline = 1;
    }

    OsReadinessState state;
    int status = __libc_os_readiness_begin(&state, mask);
    if (status) {
        errno = -status;
        return -1;
    }
    LibcPollDescriptor pins[LIBC_DESCRIPTOR_CAPACITY] = {{0}};
    for (nfds_t index = 0; index < count; ++index) {
        if (descriptors[index].fd >= 0)
            libcPinDescriptor(descriptors[index].fd, &pins[index]);
    }

    int result = 0, error = 0, expired = 0;
    for (;;) {
        state.generation = __libc_os_readiness_generation();
        result           = 0;
        for (nfds_t index = 0; index < count; ++index) {
            struct pollfd* item = &descriptors[index];
            item->revents       = 0;
            if (item->fd < 0) continue;
            if (!pins[index].description ||
                !libcPinnedDescriptorValid(item->fd, &pins[index])) {
                item->revents = POLLNVAL;
            } else {
                OpenDescription* description = pins[index].description;
                int ready = __libc_os_descriptor_ready(description->kind,
                                                       description->handle);
                if (ready == -EBADF) {
                    item->revents = POLLNVAL;
                } else if (ready < 0) {
                    error = -ready;
                    break;
                } else {
                    short automatic = POLLNVAL;
                    // select 的 except-only 集合不能被无关 HUP/ERR 提前结束。
                    if (!selecting || (item->events & (POLLIN | POLLRDNORM |
                                                       POLLOUT | POLLWRNORM)))
                        automatic |= POLLHUP | POLLERR;
                    item->revents = ready & (item->events | automatic);
                }
                if (!libcPinnedDescriptorValid(item->fd, &pins[index]))
                    item->revents = POLLNVAL;
            }
            if (item->revents) ++result;
        }
        if (error || result || expired) break;
        // 即使期限已到也由内核检查信号，不能跳过零超时下的待决信号。
        status = __libc_os_readiness_wait(&state, deadline);
        if (status == -ETIMEDOUT) expired = 1;
        if (status && status != -EAGAIN && status != -ETIMEDOUT) {
            error = -status;
            break;
        }
    }
    for (nfds_t index = 0; index < count; ++index)
        libcUnpinDescriptor(&pins[index]);
    __libc_os_readiness_end(state.token);
    errno = error ? error : saved_errno;
    return error ? -1 : result;
}

int ppoll(struct pollfd* descriptors, nfds_t count,
          const struct timespec* restrict timeout,
          const sigset_t* restrict mask) {
    return __libc_poll_wait(descriptors, count, timeout, mask, 0);
}

int poll(struct pollfd* descriptors, nfds_t count, int milliseconds) {
    struct timespec timeout;
    if (milliseconds >= 0) {
        timeout.tv_sec  = milliseconds / 1000;
        timeout.tv_nsec = (milliseconds % 1000) * 1000000L;
    }
    return ppoll(descriptors, count, milliseconds < 0 ? NULL : &timeout, NULL);
}
