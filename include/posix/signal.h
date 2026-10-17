#ifndef _POSIX_SIGNAL_H
#define _POSIX_SIGNAL_H


#include <stdint.h>
#include <sys/types.h>
#include <time.h>

typedef uint64_t sigset_t;
typedef int sig_atomic_t;
typedef struct { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
#define SS_ONSTACK 1
#define SS_DISABLE 2
#define MINSIGSTKSZ 2048
#define SIGSTKSZ 8192
typedef void (*sighandler_t)(int);

typedef struct {
    int si_signo;
    int si_code;
    int si_errno;
    pid_t si_pid;
    uid_t si_uid;
    void* si_addr;
    int si_status;
    int64_t si_value;
} siginfo_t;

struct sigevent {
    int sigev_notify;
    int sigev_signo;
    int64_t sigev_value;
    void (*sigev_notify_function)(int64_t);
    void* sigev_notify_attributes;
};

#define SIGEV_NONE 0
#define SIGEV_SIGNAL 1
#define SIGEV_THREAD 2

struct sigaction {
    union {
        sighandler_t handler;
        void (*action)(int, siginfo_t*, void*);
    } __handler;
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

#define sa_handler __handler.handler
#define sa_sigaction __handler.action

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGRTMIN 32
#define SIGRTMAX 64
#define NSIG 65

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_SIGINFO 0x00000004
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND ((int)0x80000000U)

#ifdef __cplusplus
extern "C" {
#endif

int kill(pid_t task_id, int signal);
int raise(int signal);
sighandler_t signal(int signal, sighandler_t handler);
int sigaction(int signal, const struct sigaction* action,
              struct sigaction* old_action);
int sigemptyset(sigset_t* set);
int sigfillset(sigset_t* set);
int sigaddset(sigset_t* set, int signal);
int sigdelset(sigset_t* set, int signal);
int sigismember(const sigset_t* set, int signal);
int sigprocmask(int how, const sigset_t* set, sigset_t* old_set);
int pthread_sigmask(int how, const sigset_t* set, sigset_t* old_set);
int sigpending(sigset_t* set);
int sigsuspend(const sigset_t* mask);
int sigwait(const sigset_t* set, int* signal);
int sigwaitinfo(const sigset_t* set, siginfo_t* info);
int sigtimedwait(const sigset_t* set, siginfo_t* info,
                 const struct timespec* timeout);
int pause(void);
int pthread_kill(uint64_t thread, int signal);
pid_t setsid(void);

#ifdef __cplusplus
}
#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_sigset_t
#define __DEFINED_sig_atomic_t
#define __DEFINED_siginfo_t

/* Additional POSIX types. */
union sigval { int sival_int; void *sival_ptr; };

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int killpg(pid_t, int);
void psiginfo(const siginfo_t *, const char *);
void psignal(int, const char *);
int sig2str(int, char *);
int sigaltstack(const stack_t *__restrict, stack_t *__restrict);
int sigqueue(pid_t, int, union sigval);
int str2sig(const char *__restrict, int *__restrict);
#ifdef __cplusplus
}
#endif

#endif
