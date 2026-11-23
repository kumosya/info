/*
 * select 和 pselect 共用 poll 的就绪判断，避免两套描述符等待规则发生偏差。
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>

#include "internal.h"
#include <sys/select.h>

__attribute__((visibility("hidden"))) int __libc_poll_wait(
    struct pollfd *, nfds_t, const struct timespec *, const sigset_t *, int);

/*
 * 先保存调用者的集合，等待成功后才覆盖输出；同一个描述符可以出现在多个集合中。
 * poll 的 HUP/ERR 必须转换成可读/可写，让调用者继续 read/write 获取 EOF
 * 或错误。
 */
static int wait_sets(int count, fd_set *reads, fd_set *writes, fd_set *excepts,
                     const struct timespec *timeout, const sigset_t *mask) {
    if (count < 0 || count > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }

    if ((reads && !__libc_os_accessible(reads, sizeof(*reads), 1)) ||
        (writes && !__libc_os_accessible(writes, sizeof(*writes), 1)) ||
        (excepts && !__libc_os_accessible(excepts, sizeof(*excepts), 1)) ||
        (timeout && !__libc_os_accessible(timeout, sizeof(*timeout), 0))) {
        errno = EFAULT;
        return -1;
    }

    if (timeout && (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
                    timeout->tv_nsec >= 1000000000L)) {
        errno = EINVAL;
        return -1;
    }

    struct pollfd *items = count ? calloc((size_t)count, sizeof(*items)) : NULL;
    if (count && !items) {
        return -1;
    }

    nfds_t used = 0;
    for (int fd = 0; fd < count; ++fd) {
        short events = 0;
        if (reads && FD_ISSET(fd, reads)) {
            events |= POLLIN | POLLRDNORM;
        }
        if (writes && FD_ISSET(fd, writes)) {
            events |= POLLOUT | POLLWRNORM;
        }
        if (excepts && FD_ISSET(fd, excepts)) {
            events |= POLLPRI;
        }
        if (events && fd >= LIBC_DESCRIPTOR_CAPACITY) {
            free(items);
            errno = EBADF;
            return -1;
        }
        if (events) {
            items[used].fd       = fd;
            items[used++].events = events;
        }
    }

    int result = __libc_poll_wait(items, used, timeout, mask, 1);
    if (result < 0) {
        int error = errno;
        free(items);
        errno = error;
        return -1;
    }

    /* select 对无效描述符整体报错，不把 POLLNVAL 当成一个就绪结果。 */
    for (nfds_t index = 0; index < used; ++index) {
        if (items[index].revents & POLLNVAL) {
            free(items);
            errno = EBADF;
            return -1;
        }
    }

    fd_set ready_reads, ready_writes, ready_excepts;
    FD_ZERO(&ready_reads);
    FD_ZERO(&ready_writes);
    FD_ZERO(&ready_excepts);
    result = 0;

    for (nfds_t index = 0; index < used; ++index) {
        int fd      = items[index].fd;
        short ready = items[index].revents;
        if (reads && FD_ISSET(fd, reads) &&
            (ready & (POLLIN | POLLRDNORM | POLLHUP | POLLERR))) {
            FD_SET(fd, &ready_reads);
            ++result;
        }
        if (writes && FD_ISSET(fd, writes) &&
            (ready & (POLLOUT | POLLWRNORM | POLLERR))) {
            FD_SET(fd, &ready_writes);
            ++result;
        }
        if (excepts && FD_ISSET(fd, excepts) && (ready & POLLPRI)) {
            FD_SET(fd, &ready_excepts);
            ++result;
        }
    }

    free(items);
    if (reads) {
        *reads = ready_reads;
    }
    if (writes) {
        *writes = ready_writes;
    }
    if (excepts) {
        *excepts = ready_excepts;
    }
    return result;
}

/* pselect 保留调用者的超时对象，并把临时信号掩码交给底层原子等待。 */
int pselect(int count, fd_set *restrict reads, fd_set *restrict writes,
            fd_set *restrict excepts, const struct timespec *restrict timeout,
            const sigset_t *restrict mask) {
    return wait_sets(count, reads, writes, excepts, timeout, mask);
}

/* select 的微秒期限转换成纳秒，并按单调时钟回写剩余期限。 */
int select(int count, fd_set *restrict reads, fd_set *restrict writes,
           fd_set *restrict excepts, struct timeval *restrict timeout) {
    struct timespec duration;
    if (timeout) {
        if (!__libc_os_accessible(timeout, sizeof(*timeout), 1)) {
            errno = EFAULT;
            return -1;
        }
        if (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
            timeout->tv_usec >= 1000000) {
            errno = EINVAL;
            return -1;
        }
        duration.tv_sec  = timeout->tv_sec;
        duration.tv_nsec = timeout->tv_usec * 1000L;
    }

    uint64_t start = timeout ? __libc_os_get_monotonic_time() : 0;
    int result     = wait_sets(count, reads, writes, excepts,
                           timeout ? &duration : NULL, NULL);
    int error      = errno;
    if (timeout && (result >= 0 || error == EINTR)) {
        uint64_t elapsed = __libc_os_get_monotonic_time() - start;
        uint64_t seconds = elapsed / 1000000000ULL;
        uint64_t micros  = (elapsed % 1000000000ULL) / 1000;
        if (seconds > (uint64_t)timeout->tv_sec ||
            (seconds == (uint64_t)timeout->tv_sec &&
             micros >= (uint64_t)timeout->tv_usec)) {
            timeout->tv_sec = timeout->tv_usec = 0;
        } else {
            timeout->tv_sec -= seconds;
            if ((uint64_t)timeout->tv_usec < micros) {
                --timeout->tv_sec;
                timeout->tv_usec += 1000000;
            }
            timeout->tv_usec -= micros;
        }
    }
    errno = error;
    return result;
}
