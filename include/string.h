#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
void* memcpy(void* destination, const void* source, size_t size);
void* memmove(void* destination, const void* source, size_t size);
void* memset(void* destination, int value, size_t size);
int memcmp(const void* left, const void* right, size_t size);
size_t strlen(const char* string);
size_t strnlen(const char* string, size_t maximum);
char* strcpy(char* destination, const char* source);
char* strncpy(char* destination, const char* source, size_t size);
char* strcat(char* destination, const char* source);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, size_t size);
char* strchr(const char* string, int character);
char* strrchr(const char* string, int character);
char* strstr(const char* string, const char* search);
char* strdup(const char* string);
char* strerror(int errorNumber);
#ifdef __cplusplus
}
#endif
