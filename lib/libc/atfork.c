#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/syscall.h>
#include "lock.h"

typedef struct { void (*prepare)(void), (*parent)(void), (*child)(void); } ForkHandler;
static ForkHandler handlers[64];
static unsigned count;
static volatile uint32_t handlersLock, forkLock;
extern void __libc_environment_fork_prepare(void), __libc_environment_fork_parent(void), __libc_environment_fork_child(void);
extern void __libc_random_fork_prepare(void), __libc_random_fork_parent(void), __libc_random_fork_child(void);
extern void __libc_descriptors_fork_prepare(void), __libc_descriptors_fork_parent(void), __libc_descriptors_fork_child(void);
extern void __libc_pthread_fork_prepare(void), __libc_pthread_fork_parent(void), __libc_pthread_fork_child(uint64_t);
extern void __libc_errno_fork_child(uint64_t);
extern int __libc_fork_child_runtime(void);
extern void __osinfo_runtime_fork_prepare(void), __osinfo_runtime_fork_parent(void), __osinfo_runtime_fork_child(void);

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
    libcWaitLock(&handlersLock);
    if (count == 64) { libcWaitUnlock(&handlersLock); return ENOMEM; }
    handlers[count] = (ForkHandler){prepare, parent, child}; ++count;
    libcWaitUnlock(&handlersLock); return 0;
}
int64_t __libc_fork(void) {
    ForkHandler snapshot[64]; unsigned size;
    libcWaitLock(&forkLock);
    libcWaitLock(&handlersLock);
    size = count;
    for (unsigned i = 0; i < size; ++i) snapshot[i] = handlers[i];
    libcWaitUnlock(&handlersLock);
    for (unsigned i = size; i; --i) if (snapshot[i - 1].prepare) snapshot[i - 1].prepare();
    const int64_t endpoint = __libc_os_fork_endpoint();
    uint64_t oldMask = 0;
    if (endpoint <= 0 || __libc_os_set_signal_mask(2, UINT64_MAX, &oldMask)) {
        if (endpoint > 0) __libc_os_close_handle(endpoint);
        libcWaitUnlock(&forkLock);
        for (unsigned i = 0; i < size; ++i) if (snapshot[i].parent) snapshot[i].parent();
        return -5;
    }
    __libc_pthread_fork_prepare();
    __libc_descriptors_fork_prepare();
    __libc_environment_fork_prepare();
    __libc_random_fork_prepare();
    /* Keep concurrent registration coherent for future forks in the child. */
    libcWaitLock(&handlersLock);
    __osinfo_runtime_fork_prepare();
    const uint64_t previous = __libc_os_get_thread_id();
    int64_t result = __libc_os_fork_task();
    if (!result) {
        handlersLock = 0; forkLock = 0;
        __osinfo_runtime_fork_child();
        __libc_errno_fork_child(previous);
        __libc_random_fork_child();
        __libc_environment_fork_child();
        __libc_descriptors_fork_child();
        __libc_pthread_fork_child(previous);
        const int error = __libc_fork_child_runtime();
        const int notified = __libc_os_fork_notify(endpoint, error);
        __libc_os_close_handle(endpoint);
        uint64_t ignored;
        __libc_os_set_signal_mask(2, oldMask, &ignored);
        if (error || notified) __libc_os_exit_task(127);
        for (unsigned i = 0; i < size; ++i) if (snapshot[i].child) snapshot[i].child();
    } else {
        const int handshake = result > 0 ? __libc_os_fork_receive(endpoint, result) : 0;
        __libc_os_close_handle(endpoint);
        __osinfo_runtime_fork_parent();
        libcWaitUnlock(&handlersLock);
        __libc_random_fork_parent();
        __libc_environment_fork_parent();
        __libc_descriptors_fork_parent();
        __libc_pthread_fork_parent();
        libcWaitUnlock(&forkLock);
        if (handshake) {
            __libc_os_send_signal(result, 9);
            int status;
            while (__libc_os_wait_child(result, &status, 0) == -10) {}
            result = -6;
        }
        uint64_t ignored;
        __libc_os_set_signal_mask(2, oldMask, &ignored);
        for (unsigned i = 0; i < size; ++i) if (snapshot[i].parent) snapshot[i].parent();
    }
    return result;
}
