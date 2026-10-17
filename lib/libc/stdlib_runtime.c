#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "os_adapter.h"
#include "internal.h"

#define EXIT_HANDLER_CAPACITY 16

static void (*exitHandlers[EXIT_HANDLER_CAPACITY])(void);
static uint32_t exitHandlerCount;
static uint32_t randomState = 1;

int atoi(const char* string) { return (int)strtol(string, NULL, 10); }

int rand(void) {
    randomState = randomState * 1103515245U + 12345U;
    return (int)((randomState >> 16) & RAND_MAX);
}

void srand(unsigned int seed) { randomState = seed; }

int atexit(void (*function)(void)) {
    if (function == NULL || exitHandlerCount == EXIT_HANDLER_CAPACITY) {
        return -1;
    }
    exitHandlers[exitHandlerCount++] = function;
    return 0;
}

void __libc_run_exit_handlers(void) {
    while (exitHandlerCount != 0) exitHandlers[--exitHandlerCount]();
}

void exit(int status) {
    __libc_run_exit_handlers();
    __libc_os_runtime_fini();
    libcCloseAllDescriptors();
    __libc_os_exit_task(status);
}

void _exit(int status) {
    libcCloseAllDescriptors();
    __libc_os_exit_task(status);
}
void abort(void) {
    libcCloseAllDescriptors();
    __libc_os_exit_task(134);
}
