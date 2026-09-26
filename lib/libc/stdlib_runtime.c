#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "os/c_api.h"
#include "internal.h"

#define EXIT_HANDLER_CAPACITY 16

static void (*exitHandlers[EXIT_HANDLER_CAPACITY])(void);
static uint32_t exitHandlerCount;
static uint32_t randomState = 1;

static int parseDigit(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'z') return character - 'a' + 10;
    if (character >= 'A' && character <= 'Z') return character - 'A' + 10;
    return -1;
}

unsigned long strtoul(const char* string, char** end, int base) {
    unsigned long result = 0;
    const char* current;
    int digit;
    while (string != NULL && isspace(*string)) ++string;
    if (base == 0) base = 10;
    current = string;
    while (current != NULL && (digit = parseDigit(*current)) >= 0 &&
           digit < base) {
        result = result * (unsigned long)base + (unsigned long)digit;
        ++current;
    }
    if (end != NULL) *end = (char*)current;
    return result;
}

long strtol(const char* string, char** end, int base) {
    int negative;
    unsigned long value;
    while (string != NULL && isspace(*string)) ++string;
    negative = string != NULL && *string == '-';
    if (negative || (string != NULL && *string == '+')) ++string;
    value = strtoul(string, end, base);
    return negative ? -(long)value : (long)value;
}

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
    os_runtime_fini();
    libcCloseAllDescriptors();
    os_exit_process(status);
}

void _exit(int status) {
    libcCloseAllDescriptors();
    os_exit_process(status);
}
void abort(void) {
    libcCloseAllDescriptors();
    os_exit_process(134);
}
