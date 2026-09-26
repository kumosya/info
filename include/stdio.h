#pragma once

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)
#define BUFSIZ 1024

typedef struct FileStream {
    int fileDescriptor;
    int error;
    int endOfFile;
    int owned;
} FILE;

#ifdef __cplusplus
extern "C" {
#endif
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

int printf(const char* format, ...);
int fprintf(FILE* stream, const char* format, ...);
int snprintf(char* buffer, size_t size, const char* format, ...);
int vsnprintf(char* buffer, size_t size, const char* format, va_list arguments);
int putchar(int character);
int puts(const char* string);
int getchar(void);
int fputc(int character, FILE* stream);
int fputs(const char* string, FILE* stream);
int fgetc(FILE* stream);
char* fgets(char* buffer, int size, FILE* stream);
size_t fread(void* buffer, size_t size, size_t count, FILE* stream);
size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream);
FILE* fopen(const char* path, const char* mode);
int fclose(FILE* stream);
int fflush(FILE* stream);
int fileno(FILE* stream);
int feof(FILE* stream);
int ferror(FILE* stream);
void clearerr(FILE* stream);
void perror(const char* prefix);
#ifdef __cplusplus
}
#endif
