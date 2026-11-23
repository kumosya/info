/* 真实 libc 包装配合确定性后端，验证边界、回写与竞态重查；不替代 OS 验收。 */
#define _GNU_SOURCE 1
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>

#include "internal.h"
#include <sys/select.h>

static OpenDescription description;
static uint64_t now = 100, generation = 1, current_mask = 8, old_mask;
static int ready, invalid, race, waiting_error, queries, waits, references,
    scopes;

int __libc_os_accessible(const void* address, uint64_t size, int writing) {
    (void)size;
    (void)writing;
    return address != NULL && address != (void*)1;
}
uint64_t __libc_os_get_monotonic_time(void) { return now; }
int libcPinDescriptor(int fd, LibcPollDescriptor* pin) {
    if (fd != 7) return 0;
    pin->description = &description;
    pin->generation  = 1;
    ++references;
    return 1;
}
int libcPinnedDescriptorValid(int fd, const LibcPollDescriptor* pin) {
    return fd == 7 && pin->description == &description && !invalid;
}
void libcUnpinDescriptor(LibcPollDescriptor* pin) {
    if (pin->description) --references;
    pin->description = NULL;
}
int __libc_os_descriptor_ready(int kind, uint64_t handle) {
    (void)kind;
    (void)handle;
    ++queries;
    if (race) {
        race = 0;
        ++generation;
    }
    return ready;
}
int __libc_os_readiness_begin(OsReadinessState* state, const uint64_t* mask) {
    state->generation       = generation;
    state->signalGeneration = 0;
    state->token            = mask != NULL;
    if (mask) {
        ++scopes;
        old_mask     = current_mask;
        current_mask = *mask;
    }
    return 0;
}
uint64_t __libc_os_readiness_generation(void) { return generation; }
int __libc_os_readiness_wait(const OsReadinessState* state, uint64_t deadline) {
    ++waits;
    if (waiting_error) {
        now += 20000000;
        return waiting_error;
    }
    if (state->generation != generation) {
        ready = POLLIN;
        return -EAGAIN;
    }
    assert(deadline);
    now = deadline;
    return -ETIMEDOUT;
}
void __libc_os_readiness_end(uint64_t token) {
    if (token) {
        --scopes;
        current_mask = old_mask;
    }
}

int main(void) {
    struct pollfd items[] = {
        {-1, POLLIN, -1}, {7, POLLIN, -1}, {31, POLLIN, -1}};
    ready = POLLIN | POLLOUT | POLLHUP;
    errno = 123;
    assert(poll(items, 3, 0) == 2 && errno == 123);
    assert(!items[0].revents && items[1].revents == (POLLIN | POLLHUP));
    assert(items[2].revents == POLLNVAL && !references);
    assert(poll(NULL, 1, 0) == -1 && errno == EFAULT);
    assert(poll(items, 33, 0) == -1 && errno == EINVAL);
    struct timespec bad = {-1, 0};
    assert(ppoll(NULL, 0, &bad, NULL) == -1 && errno == EINVAL);
    bad.tv_sec  = 0;
    bad.tv_nsec = 1000000000;
    assert(ppoll(NULL, 0, &bad, NULL) == -1 && errno == EINVAL);
    bad.tv_sec  = INT64_MAX;
    bad.tv_nsec = 0;
    assert(ppoll(NULL, 0, &bad, NULL) == -1 && errno == EINVAL);
    assert(poll(NULL, 0, 3) == 0 && now == 3000100);
    ready             = 0;
    race              = 1;
    struct pollfd one = {7, POLLIN, 0};
    int before        = waits;
    assert(poll(&one, 1, -1) == 1 && one.revents == POLLIN);
    assert(waits == before + 1 && !references);
    ready      = POLLERR;
    one.events = 0;
    assert(poll(&one, 1, 0) == 1 && one.revents == POLLERR);
    invalid = 1;
    assert(poll(&one, 1, 0) == 1 && one.revents == POLLNVAL && !references);
    invalid = 0;
    ready   = POLLIN | POLLOUT;
    fd_set reads, writes;
    FD_ZERO(&reads);
    FD_ZERO(&writes);
    FD_SET(7, &reads);
    FD_SET(7, &writes);
    struct timeval zero = {0};
    assert(select(8, &reads, &writes, NULL, &zero) == 2);
    assert(FD_ISSET(7, &reads) && FD_ISSET(7, &writes));
    FD_SET(100, &reads);
    assert(select(101, &reads, NULL, NULL, &zero) == -1 && errno == EBADF);
    assert(FD_ISSET(100, &reads));
    assert(select(FD_SETSIZE + 1, NULL, NULL, NULL, &zero) == -1 &&
           errno == EINVAL);
    ready = POLLHUP;
    fd_set excepts;
    FD_ZERO(&excepts);
    FD_SET(7, &excepts);
    struct timespec except_limit = {0, 5000000};
    before                       = waits;
    uint64_t started             = now;
    assert(pselect(8, NULL, NULL, &excepts, &except_limit, NULL) == 0);
    assert(now - started >= 5000000 && waits == before + 1 &&
           !FD_ISSET(7, &excepts));
    waiting_error         = -EINTR;
    ready                 = 0;
    sigset_t mask         = 16;
    struct timespec limit = {1, 123};
    assert(pselect(0, NULL, NULL, NULL, &limit, &mask) == -1 && errno == EINTR);
    assert(limit.tv_sec == 1 && limit.tv_nsec == 123 && current_mask == 8 &&
           !scopes);
    struct timespec zero_limit = {0};
    assert(ppoll(NULL, 0, &zero_limit, &mask) == -1 && errno == EINTR);
    assert(current_mask == 8 && !scopes);
    struct timeval duration = {1, 0};
    assert(select(0, NULL, NULL, NULL, &duration) == -1 && errno == EINTR);
    assert(duration.tv_sec == 0 && duration.tv_usec == 980000);
    waiting_error = 0;
    ready         = -EIO;
    assert(poll(&one, 1, 10) == -1 && errno == EIO && !references);
    assert(queries && waits && !scopes);
    return 0;
}
