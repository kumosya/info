#define _GNU_SOURCE 1
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const names[] = {NULL, "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL",
    "USR1", "SEGV", "USR2", "PIPE", "ALRM", "TERM", "STKFLT", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU",
    "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "POLL", "PWR", "SYS"};
static int number(const char* text, unsigned maximum, unsigned* output) {
    if (!*text) return 0;
    unsigned value = 0;
    do {
        if (*text < '0' || *text > '9' || value > (maximum - (unsigned)(*text - '0')) / 10U) return 0;
        value = value * 10U + (unsigned)(*text++ - '0');
        if (value > maximum) return 0;
    } while (*text);
    *output = value; return 1;
}
int sig2str(int signal, char* output) {
    if (!output || signal <= 0 || signal >= NSIG) { errno = EINVAL; return -1; }
    if (signal < SIGRTMIN) strcpy(output, names[signal]);
    else if (signal == SIGRTMIN) strcpy(output, "RTMIN");
    else if (signal == SIGRTMAX) strcpy(output, "RTMAX");
    else snprintf(output, SIG2STR_MAX, "RTMIN+%d", signal - SIGRTMIN);
    return 0;
}
int str2sig(const char* restrict text, int* restrict output) {
    if (!text || !output) { errno = EINVAL; return -1; }
    int signal = 0; unsigned parsed;
    for (int i = 1; i < SIGRTMIN; ++i) if (!strcmp(text, names[i])) { signal = i; break; }
    if (!signal && !strcmp(text, "IOT")) signal = SIGABRT;
    if (!signal && !strcmp(text, "CLD")) signal = SIGCHLD;
    if (!signal && !strcmp(text, "IO")) signal = SIGPOLL;
    if (!signal && !strcmp(text, "RTMIN")) signal = SIGRTMIN;
    if (!signal && !strcmp(text, "RTMAX")) signal = SIGRTMAX;
    if (!signal && !strncmp(text, "RTMIN+", 6) && number(text + 6, SIGRTMAX - SIGRTMIN, &parsed)) signal = SIGRTMIN + parsed;
    if (!signal && !strncmp(text, "RTMAX-", 6) && number(text + 6, SIGRTMAX - SIGRTMIN, &parsed)) signal = SIGRTMAX - parsed;
    if (!signal && number(text, NSIG - 1, &parsed)) signal = (int)parsed;
    if (!signal) { errno = EINVAL; return -1; }
    *output = signal; return 0;
}
void psignal(int signal, const char* prefix) {
    if (prefix && *prefix) fprintf(stderr, "%s: %s\n", prefix, strsignal(signal));
    else fprintf(stderr, "%s\n", strsignal(signal));
}
void psiginfo(const siginfo_t* info, const char* prefix) {
    if (!info) { errno = EINVAL; return; }
    char detail[160];
    if (info->si_signo == SIGCHLD && info->si_code >= CLD_EXITED && info->si_code <= CLD_CONTINUED)
        snprintf(detail, sizeof(detail), "%s (code=%d, pid=%d, uid=%u, status=%d)", strsignal(info->si_signo), info->si_code, info->si_pid, info->si_uid, info->si_status);
    else if (info->si_code == SI_USER || info->si_code == -1 || info->si_code == -6)
        snprintf(detail, sizeof(detail), "%s (code=%d, pid=%d, uid=%u)", strsignal(info->si_signo), info->si_code, info->si_pid, info->si_uid);
    else if (info->si_code > 0 &&
        ((info->si_signo == SIGILL && info->si_code <= 8) || (info->si_signo == SIGFPE && info->si_code <= 8) ||
         (info->si_signo == SIGSEGV && (info->si_code <= 3 || info->si_code == SEGV_CPERR)) ||
         (info->si_signo == SIGBUS && info->si_code <= 3) || (info->si_signo == SIGTRAP && info->si_code <= 4)))
        snprintf(detail, sizeof(detail), "%s (code=%d, address=%p)", strsignal(info->si_signo), info->si_code, info->si_addr);
    else snprintf(detail, sizeof(detail), "%s (code=%d)", strsignal(info->si_signo), info->si_code);
    if (prefix && *prefix) fprintf(stderr, "%s: %s\n", prefix, detail);
    else fprintf(stderr, "%s\n", detail);
}
