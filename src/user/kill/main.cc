#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argumentCount, char** arguments) {
    if (argumentCount != 2) {
        fputs("usage: kill PID\n", stderr);
        return 1;
    }
    char* end                     = nullptr;
    const unsigned long processId = strtoul(arguments[1], &end, 10);
    if (processId == 0 || end == nullptr || *end != '\0' ||
        kill((pid_t)processId, SIGTERM) != 0) {
        fputs("kill: process not found or not permitted\n", stderr);
        return 1;
    }
    return 0;
}
