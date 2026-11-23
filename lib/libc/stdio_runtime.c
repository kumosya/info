#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static FILE standardInput = {STDIN_FILENO, 0, 0, 0, EOF, 0, NULL};
static FILE standardOutput = {STDOUT_FILENO, 0, 0, 0, EOF, 0, NULL};
static FILE standardError = {STDERR_FILENO, 0, 0, 0, EOF, 0, NULL};
FILE* stdin = &standardInput;
FILE* stdout = &standardOutput;
FILE* stderr = &standardError;

int snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int printf(const char* format, ...) {
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}

void perror(const char* prefix) {
    fprintf(stderr, "%s%s%s\n", prefix == NULL ? "" : prefix,
            prefix != NULL && *prefix != '\0' ? ": " : "", strerror(errno));
}
