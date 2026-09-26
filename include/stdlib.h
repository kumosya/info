#pragma once

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767

#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* pointer, size_t size);
void free(void* pointer);
char* getenv(const char* name);
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);
int clearenv(void);
int atoi(const char* string);
long strtol(const char* string, char** end, int base);
unsigned long strtoul(const char* string, char** end, int base);
int rand(void);
void srand(unsigned int seed);
int atexit(void (*function)(void));
void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
