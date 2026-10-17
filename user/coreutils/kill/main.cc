#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../signals.h"

int main(int argumentCount, char** arguments) {
    int signal = SIGTERM, index = 1;
    if (index < argumentCount && !strcmp(arguments[index], "-l")) {
        for (unsigned i = 1; i < sizeof(coreutils::signalNames) / sizeof(coreutils::signalNames[0]); ++i)
            printf("%u) SIG%s%s", i, coreutils::signalNames[i], i % 5 ? "  " : "\n");
        puts(""); return 0;
    }
    if (index < argumentCount && !strcmp(arguments[index], "-s")) {
        if (++index >= argumentCount) return 2;
        signal = coreutils::parseSignal(arguments[index++]);
    } else if (index < argumentCount && arguments[index][0] == '-' && strcmp(arguments[index], "--"))
        signal = coreutils::parseSignal(arguments[index++] + 1);
    if (index < argumentCount && !strcmp(arguments[index], "--")) ++index;
    if (signal < 0 || index == argumentCount) { fputs("usage: kill [-SIGNAL | -s SIGNAL] [--] PID ...\n", stderr); return 2; }
    int result = 0;
    for (; index < argumentCount; ++index) {
        char* end;
        const long pid = strtol(arguments[index], &end, 10);
        if (!*arguments[index] || *end || kill(static_cast<pid_t>(pid), signal)) {
            perror(arguments[index]); result = 1;
        }
    }
    return result;
}
