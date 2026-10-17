#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static FILE standardInput = {STDIN_FILENO, 0, 0, 0, EOF, 0};
static FILE standardOutput = {STDOUT_FILENO, 0, 0, 0, EOF, 0};
static FILE standardError = {STDERR_FILENO, 0, 0, 0, EOF, 0};
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

int fputc(int character, FILE* stream) {
    char value = (char)character;
    return stream != NULL && write(stream->fileDescriptor, &value, 1) == 1
               ? (unsigned char)value
               : EOF;
}

int putchar(int character) { return fputc(character, stdout); }

int fputs(const char* string, FILE* stream) {
    size_t length;
    if (string == NULL || stream == NULL) return EOF;
    length = strlen(string);
    return write(stream->fileDescriptor, string, length) == (ssize_t)length
               ? 0
               : EOF;
}

int puts(const char* string) {
    return fputs(string, stdout) == 0 && fputc('\n', stdout) != EOF ? 0 : EOF;
}

int fgetc(FILE* stream) {
    char value;
    if (stream && stream->pushed != EOF) { int c=stream->pushed; stream->pushed=EOF; return c; }
    ssize_t result=stream?read(stream->fileDescriptor,&value,1):-1;
    if (result != 1) {
        if (stream != NULL) { stream->endOfFile = result==0; stream->error=result<0; }
        return EOF;
    }
    return (unsigned char)value;
}

int getchar(void) { return fgetc(stdin); }

char* fgets(char* buffer, int size, FILE* stream) {
    int used = 0;
    if (buffer == NULL || size <= 0 || stream == NULL) return NULL;
    while (used + 1 < size) {
        int character = fgetc(stream);
        if (character == EOF) break;
        buffer[used++] = (char)character;
        if (character == '\n') break;
    }
    buffer[used] = '\0';
    return used == 0 ? NULL : buffer;
}

size_t fread(void* buffer, size_t size, size_t count, FILE* stream) {
    ssize_t result;
    if (size == 0 || count == 0 || stream == NULL) return 0;
    result = read(stream->fileDescriptor, buffer, size * count);
    if (result <= 0) {
        stream->endOfFile = result == 0;
        stream->error = result < 0;
        return 0;
    }
    return (size_t)result / size;
}

size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream) {
    ssize_t result;
    if (size == 0 || count == 0 || stream == NULL) return 0;
    result = write(stream->fileDescriptor, buffer, size * count);
    if (result < 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t)result / size;
}

FILE* fopen(const char* path, const char* mode) {
    int flags = O_RDONLY;
    int fileDescriptor;
    FILE* stream;
    if (!mode || !*mode || !strchr("rwa",*mode)) { errno=EINVAL; return NULL; }
    if (mode != NULL && mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (mode != NULL && mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
    if (strchr(mode,'+')) flags=(flags&~O_ACCMODE)|O_RDWR;
    if (strchr(mode,'e')) flags|=O_CLOEXEC;
    if (strchr(mode,'x')) flags|=O_EXCL;
    fileDescriptor = open(path, flags, 0644);
    if (fileDescriptor < 0) return NULL;
    stream = malloc(sizeof(*stream));
    if (stream == NULL) {
        close(fileDescriptor);
        return NULL;
    }
    stream->fileDescriptor = fileDescriptor;
    stream->error = 0;
    stream->endOfFile = 0;
    stream->owned = 1;
    stream->pushed = EOF;
    stream->orientation = 0;
    return stream;
}

int fclose(FILE* stream) {
    int result;
    if (stream == NULL) return EOF;
    result = close(stream->fileDescriptor);
    if (stream != stdin && stream != stdout && stream != stderr) free(stream);
    return result;
}

int fflush(FILE* stream) { (void)stream; return 0; }
int fileno(FILE* stream) { return stream == NULL ? -1 : stream->fileDescriptor; }
int feof(FILE* stream) { return stream == NULL ? 0 : stream->endOfFile; }
int ferror(FILE* stream) { return stream == NULL ? 1 : stream->error; }
void clearerr(FILE* stream) {
    if (stream != NULL) stream->error = stream->endOfFile = 0;
}

void perror(const char* prefix) {
    fprintf(stderr, "%s%s%s\n", prefix == NULL ? "" : prefix,
            prefix != NULL && *prefix != '\0' ? ": " : "", strerror(errno));
}
