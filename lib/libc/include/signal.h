/* Public domain.  */
#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* Signal number definitions */
#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGIOT     6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE    13
#define SIGALRM    14
#define SIGTERM    15
#define SIGSTKFLT  16
#define SIGCHLD    17
#define SIGCONT    18
#define SIGSTOP    19
#define SIGTSTP    20
#define SIGTTIN    21
#define SIGTTOU    22
#define SIGURG     23
#define SIGXCPU    24
#define SIGXFSZ    25
#define SIGVTALRM  26
#define SIGPROF    27
#define SIGWINCH   28
#define SIGIO      29
#define SIGPOLL    SIGIO
#define SIGPWR     30
#define SIGSYS     31
#define SIGUNUSED  31

/* Signal action flags */
#define SA_NOCLDSTOP 1
#define SA_NOCLDWAIT 2
#define SA_SIGINFO   4
#define SA_RESTART   8
#define SA_ONSTACK   16
#define SA_RESETHAND 32
#define SA_NODEFER   64
#define SA_NOMASK    SA_NODEFER

/* Signal mask manipulation */
#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2

/* Signal set operations */
#define _SIGSET_NWORDS (1024 / (8 * sizeof(unsigned long)))

typedef struct {
    unsigned long __val[_SIGSET_NWORDS];
} sigset_t;

/* sigaction structure */
struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

/* siginfo structure */
struct siginfo_t {
    int si_signo;
    int si_errno;
    int si_code;
    pid_t si_pid;
    uid_t si_uid;
    void *si_addr;
    int si_status;
    void *si_value;
};

/* Function declarations */
typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);
int raise(int signum);
int kill(pid_t pid, int signum);
int sigsuspend(const sigset_t *mask);
unsigned int alarm(unsigned int seconds);

#ifdef __cplusplus
}
#endif

#endif /* _SIGNAL_H */