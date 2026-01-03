#ifndef _WCHAR_H
#define _WCHAR_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Types */

typedef unsigned int wctype_t;
typedef int wint_t;
#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif

typedef struct {
    int __count;
    union {
        wint_t __wch;
        char __wchb[4];
    } __value;
} mbstate_t;

struct tm;

/* Constants */

#define WCHAR_MIN __WCHAR_MIN__
#define WCHAR_MAX __WCHAR_MAX__
#define WEOF (-1)

/* Functions for wide character classification */

int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);
int iswcntrl(wint_t wc);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);
int iswctype(wint_t wc, wctype_t desc);

/* Wide character case mapping */

wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);
wctype_t wctype(const char *property);

/* Wide character and string conversion */

int mblen(const char *s, size_t n);
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n);
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n);
wint_t btowc(int c);
int wctob(wint_t c);

/* Multibyte/wide character conversion with state */

size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);

/* Wide string functions */

wchar_t *wcscpy(wchar_t *s1, const wchar_t *s2);
wchar_t *wcscat(wchar_t *s1, const wchar_t *s2);
int wcscmp(const wchar_t *s1, const wchar_t *s2);
size_t wcslen(const wchar_t *s);
wchar_t *wcsncat(wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wcsncpy(wchar_t *s1, const wchar_t *s2, size_t n);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
size_t wcscspn(const wchar_t *s1, const wchar_t *s2);
size_t wcsspn(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcspbrk(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcsstr(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcstok(wchar_t *s1, const wchar_t *s2, wchar_t **ptr);

/* Memory operations for wide characters */

void *wmemcpy(void *s1, const void *s2, size_t n);
void *wmemmove(void *s1, const void *s2, size_t n);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
void *wmemset(void *s, wchar_t c, size_t n);

/* File I/O with wide characters */

wint_t fgetwc(FILE *stream);
wchar_t *fgetws(wchar_t *ws, int n, FILE *stream);
wint_t fputwc(wchar_t wc, FILE *stream);
int fputws(const wchar_t *ws, FILE *stream);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t putwc(wchar_t wc, FILE *stream);
wint_t putwchar(wchar_t wc);

/* Wide character formatted I/O */

int fwprintf(FILE *stream, const wchar_t *format, ...);
int swprintf(wchar_t *ws, size_t n, const wchar_t *format, ...);
int wprintf(const wchar_t *format, ...);
int vfwprintf(FILE *stream, const wchar_t *format, va_list arg);
int vswprintf(wchar_t *ws, size_t n, const wchar_t *format, va_list arg);
int vwprintf(const wchar_t *format, va_list arg);

/* Wide character file positioning */

int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);

/* Wide character string to numeric conversion */
double wcstod(const wchar_t *nptr, wchar_t **endptr);
float wcstof(const wchar_t *nptr, wchar_t **endptr);
long double wcstold(const wchar_t *nptr, wchar_t **endptr);
long int wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
long long int wcstoll(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long int wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long long int wcstoull(const wchar_t *nptr, wchar_t **endptr,
                                int base);

/* Date and time functions with wide characters */
wchar_t *wcsftime(wchar_t *s, size_t maxsize, const wchar_t *format,
                  const struct tm *timeptr);

#ifdef __cplusplus
}
#endif

#endif /* _WCHAR_H */