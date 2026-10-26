#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <os/syscall.h>
#include <sys/resource.h>

#include "internal.h"

_Static_assert(sizeof(siginfo_t) == sizeof(OsSignalInfo), "signal info ABI");
_Static_assert(sizeof(ucontext_t) == sizeof(OsUserContext), "signal context ABI");
_Static_assert(offsetof(ucontext_t, uc_mcontext) == offsetof(OsUserContext, machine), "machine context ABI");

static int valid_signal(int signal_number) {
    return signal_number > 0 && signal_number < NSIG;
}

__attribute__((naked, noreturn)) static void signal_restorer(void) {
    __asm__ volatile("movq %rsp, %rdi\n\t"
                     "andq $-16, %rsp\n\t"
                     "call __libc_os_signal_return\n\t"
                     "ud2");
}

int sigemptyset(sigset_t* set) {
    if (set == NULL) {
        errno = EINVAL;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t* set) {
    if (set == NULL) {
        errno = EINVAL;
        return -1;
    }
    *set = UINT64_MAX;
    return 0;
}

int sigaddset(sigset_t* set, int signal_number) {
    if (set == NULL || !valid_signal(signal_number)) {
        errno = EINVAL;
        return -1;
    }
    *set |= 1ULL << (signal_number - 1);
    return 0;
}

int sigdelset(sigset_t* set, int signal_number) {
    if (set == NULL || !valid_signal(signal_number)) {
        errno = EINVAL;
        return -1;
    }
    *set &= ~(1ULL << (signal_number - 1));
    return 0;
}

int sigismember(const sigset_t* set, int signal_number) {
    if (set == NULL || !valid_signal(signal_number)) {
        errno = EINVAL;
        return -1;
    }
    return (*set & (1ULL << (signal_number - 1))) != 0;
}

int sigaction(int signal_number, const struct sigaction* action,
              struct sigaction* old_action) {
    OsSignalAction kernel_action;
    OsSignalAction kernel_old;
    const OsSignalAction* new_pointer = NULL;
    if (!valid_signal(signal_number) || signal_number == SIGKILL ||
        signal_number == SIGSTOP) {
        errno = EINVAL;
        return -1;
    }
    memset(&kernel_action, 0, sizeof(kernel_action));
    memset(&kernel_old, 0, sizeof(kernel_old));
    if (action != NULL) {
        if ((action->sa_flags & ~(SA_SIGINFO | SA_RESTART | SA_NODEFER |
                                 SA_RESETHAND | SA_ONSTACK)) != 0) {
            errno = EINVAL;
            return -1;
        }
        kernel_action.handler = (uint64_t)(uintptr_t)action->sa_handler;
        kernel_action.mask = action->sa_mask;
        kernel_action.flags = (uint64_t)(uint32_t)action->sa_flags;
        kernel_action.restorer = (uint64_t)(uintptr_t)signal_restorer;
        new_pointer = &kernel_action;
    }
    if (__libc_os_set_signal_action((uint32_t)signal_number, new_pointer,
                             &kernel_old) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (old_action != NULL) {
        memset(old_action, 0, sizeof(*old_action));
        old_action->sa_handler = (sighandler_t)(uintptr_t)kernel_old.handler;
        old_action->sa_mask = kernel_old.mask; old_action->sa_flags = (int)kernel_old.flags;
        old_action->sa_restorer = (void(*)(void))(uintptr_t)kernel_old.restorer;
    }
    return 0;
}

sighandler_t signal(int signal_number, sighandler_t handler) {
    struct sigaction action;
    struct sigaction old_action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    action.sa_flags = SA_RESTART;
    if (sigaction(signal_number, &action, &old_action) != 0) return SIG_ERR;
    return old_action.sa_handler;
}

int raise(int signal_number) { return kill(getpid(), signal_number); }

int pthread_sigmask(int how, const sigset_t* set, sigset_t* old_set) {
    uint64_t old_mask = 0;
    if (how < SIG_BLOCK || how > SIG_SETMASK) return EINVAL;
    if (__libc_os_set_signal_mask(set == NULL ? SIG_BLOCK : how, set == NULL ? 0 : *set, &old_mask) != 0) {
        return EINVAL;
    }
    if (old_set != NULL) *old_set = old_mask;
    return 0;
}

int sigprocmask(int how, const sigset_t* set, sigset_t* old_set) {
    int result = pthread_sigmask(how, set, old_set);
    if (result != 0) {
        errno = result;
        return -1;
    }
    return 0;
}

int sigpending(sigset_t* set) {
    if (set == NULL || __libc_os_get_pending_signals(set) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int sigwait(const sigset_t* set, int* signal_number) {
    int result;
    if (set == NULL || signal_number == NULL || *set == 0) return EINVAL;
    while ((result = __libc_os_wait_signal(*set)) < 0) {
        if (result != -6) return EINVAL;
        __libc_os_yield();
    }
    *signal_number = result;
    return 0;
}

int sigwaitinfo(const sigset_t* set, siginfo_t* info) {
    int signal_number;
    int result = sigwait(set, &signal_number);
    if (result != 0) {
        errno = result;
        return -1;
    }
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->si_signo = signal_number;
    }
    return signal_number;
}

int sigtimedwait(const sigset_t* set, siginfo_t* info,
                 const struct timespec* timeout) {
    uint64_t deadline;
    int result;
    if (set == NULL || timeout == NULL || timeout->tv_sec < 0 ||
        timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    deadline = __libc_os_get_monotonic_time() +
               (uint64_t)timeout->tv_sec * 1000000000ULL +
               (uint64_t)timeout->tv_nsec;
    while ((result = __libc_os_wait_signal(*set)) < 0) {
        if (result != -6) {
            errno = EINVAL;
            return -1;
        }
        if (__libc_os_get_monotonic_time() >= deadline) {
            errno = EAGAIN;
            return -1;
        }
        __libc_os_sleep(1);
    }
    if (info != NULL) {
        memset(info, 0, sizeof(*info));
        info->si_signo = result;
    }
    return result;
}

int pause(void) {
    uint64_t generation = __libc_os_signal_generation();
    while (__libc_os_signal_generation() == generation) {
        __libc_os_sleep(1);
    }
    errno = EINTR;
    return -1;
}

int sigsuspend(const sigset_t* mask) {
    sigset_t old_mask;
    uint64_t generation = __libc_os_signal_generation();
    if (mask == NULL || pthread_sigmask(SIG_SETMASK, mask, &old_mask) != 0) {
        errno = EINVAL;
        return -1;
    }
    while (__libc_os_signal_generation() == generation) {
        __libc_os_sleep(1);
    }
    (void)pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
    errno = EINTR;
    return -1;
}

int pthread_kill(uint64_t thread, int signal_number) {
    int result;
    if (signal_number < 0 || signal_number >= NSIG) return EINVAL;
    result = __libc_os_send_thread_signal(thread, (uint32_t)signal_number);
    return result == 0 ? 0 : (result == -4 ? ESRCH : EINVAL);
}

int sigaltstack(const stack_t* stack, stack_t* old_stack) {
    if (stack && stack->ss_flags != SS_DISABLE && stack->ss_flags != 0) { errno = EINVAL; return -1; }
    if (stack && !stack->ss_flags && stack->ss_size < MINSIGSTKSZ) { errno = ENOMEM; return -1; }
    int result = __libc_os_signal_stack(stack, old_stack);
    if (result) { errno = result == -3 ? EPERM : EINVAL; return -1; }
    return 0;
}
