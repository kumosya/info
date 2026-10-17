#ifndef _COREUTILS_SIGNALS_H
#define _COREUTILS_SIGNALS_H
#include <signal.h>
#include <stdlib.h>
#include <string.h>
namespace coreutils {
inline constexpr const char* signalNames[] = {
    "0", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV",
    "USR2", "PIPE", "ALRM", "TERM", "STKFLT", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU",
    "URG", "XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "IO", "PWR", "SYS"
};
inline int parseSignal(const char* value) {
    if (!strncmp(value, "SIG", 3)) value += 3;
    for (unsigned i = 0; i < sizeof(signalNames) / sizeof(signalNames[0]); ++i)
        if (!strcmp(value, signalNames[i])) return i;
    char* end;
    const long number = strtol(value, &end, 10);
    return *value && !*end && number >= 0 && number <= 64 ? number : -1;
}
}
#endif
