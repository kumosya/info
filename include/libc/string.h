#ifndef LIBC_STRING_H
#define LIBC_STRING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#ifndef NULL
#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif
#endif

void *memcpy (void * , const void * , size_t);
void *memmove (void *, const void *, size_t);
void *memset (void *, int, size_t);
int memcmp (const void *, const void *, size_t);
void *memchr (const void *, int, size_t);

char *strcpy(char * dest, const char * src);
char *strncpy(char * dest, const char * src, size_t n);
char *strcat(char * dest, const char * src);
char *strncat(char * dest, const char * src, size_t n);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strcoll(const char *s1, const char *s2);
size_t strxfrm(char * dest, const char * src, size_t n);

char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);

char *strpbrk(const char *s, const char *charset);
char *strstr(const char *haystack, const char *needle);
char *strtok(char * s, const char * delim);

size_t strlen(const char *s);
char *strerror(int errnum);
size_t strnlen(const char *s, size_t maxlen);
char *strdup(const char *s);

#ifdef __cplusplus
}
#endif /* LIBC_STRING_H */
#endif
