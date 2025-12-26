/* Public domain.  */
#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <wchar.h>

/* 定义缺失的类型 */
typedef struct {
    int quot;
    int rem;
} div_t;
typedef struct {
    long quot;
    long rem;
} ldiv_t;
typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

/* Memory allocation functions */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

/* Process control functions */
void abort(void);
void exit(int status);
int atexit(void (*func)(void));

/* Environment functions */
extern char **environ;
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

/* Numerical conversion functions */
int atoi(const char *nptr);
long atol(const char *nptr);
long long atoll(const char *nptr);

double atof(const char *nptr);

long strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);

float strtof(const char *nptr, char **endptr);
double strtod(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);

/* Random number generation */
#define RAND_MAX 32767
int rand(void);
void srand(unsigned int seed);

/* Pseudo-terminal functions */
int system(const char *command);
char *getpass(const char *prompt);

/* Sorting and searching */
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

/* Integer arithmetic */
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
lldiv_t lldiv(long long numerator, long long denominator);

/* Multibyte character conversion */
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wc);

/* String conversion to wide string */
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n);
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n);

/* Memory allocation macros */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* Alignment related */
typedef size_t alignof_max_align_t;

#endif /* _STDLIB_H */