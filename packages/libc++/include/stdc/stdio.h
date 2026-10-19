#ifndef _STDC_STDIO_H
#define _STDC_STDIO_H


#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)
#define BUFSIZ 1024

typedef struct FileStream {
    int fileDescriptor;
    int error;
    int endOfFile;
    int owned;
    int pushed;
    int orientation;
} FILE;

#ifdef __cplusplus
extern "C" {
#endif
int rename(const char* existing, const char* newPath);
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;
#define stdin stdin
#define stdout stdout
#define stderr stderr

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

/* Additional POSIX types. */
#include <sys/types.h>
typedef off_t fpos_t;
#define L_tmpnam 64
#define TMP_MAX 256
#define FOPEN_MAX 32
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_fpos_t

/* POSIX.1-2024 declarations. */
#include <features.h>
#ifdef __cplusplus
extern "C" {
#endif
int asprintf(char **__restrict ptr, const char *__restrict format, ...);
char *ctermid(char *);
int dprintf(int, const char *__restrict, ...);
FILE *fdopen(int, const char *);
int fgetpos(FILE *__restrict, fpos_t *__restrict);
void flockfile(FILE *);
FILE *fmemopen(void *__restrict, size_t, const char *__restrict);
FILE *freopen(const char *__restrict, const char *__restrict, FILE *__restrict);
int fscanf(FILE *__restrict, const char *__restrict, ...);
int fseek(FILE *, long, int);
int fseeko(FILE *, off_t, int);
int fsetpos(FILE *, const fpos_t *);
long ftell(FILE *);
off_t ftello(FILE *);
int ftrylockfile(FILE *);
void funlockfile(FILE *);
int getc(FILE *);
int getc_unlocked(FILE *);
int getchar_unlocked(void);
ssize_t getdelim(char **__restrict, size_t *__restrict, int, FILE *__restrict);
ssize_t getline(char **__restrict, size_t *__restrict, FILE *__restrict);
FILE *open_memstream(char **, size_t *);
int pclose(FILE *);
FILE *popen(const char *, const char *);
int putc(int, FILE *);
int putc_unlocked(int, FILE *);
int putchar_unlocked(int);
int remove(const char *);
int renameat(int, const char *, int, const char *);
void rewind(FILE *);
int scanf(const char *__restrict, ...);
void setbuf(FILE *__restrict, char *__restrict);
int setvbuf(FILE *__restrict, char *__restrict, int, size_t);
int sprintf(char *__restrict, const char *__restrict, ...);
int sscanf(const char *__restrict, const char *__restrict, ...);
FILE *tmpfile(void);
char *tmpnam(char *);
int ungetc(int, FILE *);
int vasprintf(char **__restrict ptr, const char *__restrict format, va_list ap);
int vdprintf(int, const char *__restrict, va_list);
int vfprintf(FILE *__restrict, const char *__restrict, va_list);
int vfscanf(FILE *__restrict, const char *__restrict, va_list);
int vprintf(const char *__restrict, va_list);
int vscanf(const char *__restrict, va_list);
int vsprintf(char *__restrict, const char *__restrict, va_list);
int vsscanf(const char *__restrict, const char *__restrict, va_list);
#ifdef __cplusplus
}
#endif

#endif
