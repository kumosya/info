#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct OutputBuffer {
    char* data;
    size_t capacity;
    size_t length;
} OutputBuffer;

static FILE standardInput = {STDIN_FILENO, 0, 0, 0};
static FILE standardOutput = {STDOUT_FILENO, 0, 0, 0};
static FILE standardError = {STDERR_FILENO, 0, 0, 0};
FILE* stdin = &standardInput;
FILE* stdout = &standardOutput;
FILE* stderr = &standardError;

static void appendCharacter(OutputBuffer* output, char character) {
    if (output->data != NULL && output->capacity != 0 &&
        output->length + 1 < output->capacity) {
        output->data[output->length] = character;
    }
    ++output->length;
}

static void appendString(OutputBuffer* output, const char* string) {
    if (string == NULL) string = "(null)";
    while (*string != '\0') appendCharacter(output, *string++);
}

static void finishOutput(OutputBuffer* output) {
    if (output->data == NULL || output->capacity == 0) return;
    output->data[output->length < output->capacity ? output->length
                                                   : output->capacity - 1] = '\0';
}

static void appendUnsigned(OutputBuffer* output, uint64_t value, uint32_t base,
                           int upperCase) {
    char digits[32];
    uint32_t count = 0;
    do {
        uint32_t digit = (uint32_t)(value % base);
        digits[count++] = (char)(digit < 10 ? '0' + digit
                                            : (upperCase ? 'A' : 'a') + digit - 10);
        value /= base;
    } while (value != 0);
    while (count != 0) appendCharacter(output, digits[--count]);
}

int vsnprintf(char* buffer, size_t size, const char* format, va_list arguments) {
    OutputBuffer output = {buffer, size, 0};
    while (format != NULL && *format != '\0') {
        int left = 0;
        int zero = 0;
        uint32_t width = 0;
        uint32_t length = 0;
        char temporary[96];
        OutputBuffer value = {temporary, sizeof(temporary), 0};
        char conversion;
        size_t valueLength;
        size_t index;
        if (*format != '%') {
            appendCharacter(&output, *format++);
            continue;
        }
        ++format;
        if (*format == '-') {
            left = 1;
            ++format;
        }
        if (*format == '0') {
            zero = 1;
            ++format;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (uint32_t)(*format++ - '0');
        }
        while (*format == 'l') {
            ++length;
            ++format;
        }
        conversion = *format == '\0' ? '\0' : *format++;
        if (conversion == 's') {
            appendString(&value, va_arg(arguments, const char*));
        } else if (conversion == 'c') {
            appendCharacter(&value, (char)va_arg(arguments, int));
        } else if (conversion == 'd' || conversion == 'i') {
            int64_t number = length != 0 ? va_arg(arguments, int64_t)
                                         : va_arg(arguments, int);
            if (number < 0) {
                appendCharacter(&value, '-');
                appendUnsigned(&value, (uint64_t)(-(number + 1)) + 1, 10, 0);
            } else {
                appendUnsigned(&value, (uint64_t)number, 10, 0);
            }
        } else if (conversion == 'u' || conversion == 'x' || conversion == 'X') {
            uint64_t number = length != 0 ? va_arg(arguments, uint64_t)
                                          : (uint64_t)va_arg(arguments, unsigned);
            appendUnsigned(&value, number, conversion == 'u' ? 10 : 16,
                           conversion == 'X');
        } else if (conversion == 'p') {
            appendString(&value, "0x");
            appendUnsigned(&value, (uint64_t)(uintptr_t)va_arg(arguments, void*),
                           16, 0);
        } else if (conversion == '%') {
            appendCharacter(&value, '%');
        } else {
            appendCharacter(&value, '%');
            if (conversion != '\0') appendCharacter(&value, conversion);
        }
        finishOutput(&value);
        valueLength = value.length;
        if (!left) {
            for (index = valueLength; index < width; ++index) {
                appendCharacter(&output, zero ? '0' : ' ');
            }
        }
        appendString(&output, temporary);
        if (left) {
            for (index = valueLength; index < width; ++index) {
                appendCharacter(&output, ' ');
            }
        }
    }
    finishOutput(&output);
    return (int)output.length;
}

int snprintf(char* buffer, size_t size, const char* format, ...) {
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

int fprintf(FILE* stream, const char* format, ...) {
    char buffer[512];
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    return stream == NULL || write(stream->fileDescriptor, buffer,
                                   strlen(buffer)) < 0
               ? -1
               : result;
}

int printf(const char* format, ...) {
    char buffer[512];
    va_list arguments;
    int result;
    va_start(arguments, format);
    result = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    return write(STDOUT_FILENO, buffer, strlen(buffer)) < 0 ? -1 : result;
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
    if (stream == NULL || read(stream->fileDescriptor, &value, 1) != 1) {
        if (stream != NULL) stream->endOfFile = 1;
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
    if (mode != NULL && mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (mode != NULL && mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND;
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
    return stream;
}

int fclose(FILE* stream) {
    int result;
    if (stream == NULL || !stream->owned) return EOF;
    result = close(stream->fileDescriptor);
    free(stream);
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
            prefix != NULL && *prefix != '\0' ? ": " : "", "error");
}
