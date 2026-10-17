#include <stdarg.h>
/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <wchar.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) int fwprintf(FILE *restrict arg0, const wchar_t *restrict arg1, ...) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int fwscanf(FILE *restrict arg0, const wchar_t *restrict arg1, ...) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) size_t mbsnrtowcs(wchar_t *restrict arg0, const char **restrict arg1, size_t arg2, size_t arg3, mbstate_t *restrict arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; errno = ENOSYS; return (size_t)-1; }
__attribute__((weak)) FILE * open_wmemstream(wchar_t ** arg0, size_t * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (FILE *)0; }
__attribute__((weak)) int swprintf(wchar_t *restrict arg0, size_t arg1, const wchar_t *restrict arg2, ...) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int swscanf(const wchar_t *restrict arg0, const wchar_t *restrict arg1, ...) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) wint_t ungetwc(wint_t arg0, FILE * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (wint_t)-1; }
__attribute__((weak)) int vfwprintf(FILE *restrict arg0, const wchar_t *restrict arg1, va_list arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int vfwscanf(FILE *restrict arg0, const wchar_t *restrict arg1, va_list arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int vswprintf(wchar_t *restrict arg0, size_t arg1, const wchar_t *restrict arg2, va_list arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int vswscanf(const wchar_t *restrict arg0, const wchar_t *restrict arg1, va_list arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int vwprintf(const wchar_t *restrict arg0, va_list arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int vwscanf(const wchar_t *restrict arg0, va_list arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) wchar_t * wcpcpy(wchar_t *restrict arg0, const wchar_t *restrict arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (wchar_t *)0; }
__attribute__((weak)) wchar_t * wcpncpy(wchar_t *restrict arg0, const wchar_t *restrict arg1, size_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (wchar_t *)0; }
__attribute__((weak)) size_t wcsftime(wchar_t *restrict arg0, size_t arg1, const wchar_t *restrict arg2, const struct tm *restrict arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (size_t)-1; }
__attribute__((weak)) size_t wcslcat(wchar_t *restrict arg0, const wchar_t *restrict arg1, size_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (size_t)-1; }
__attribute__((weak)) size_t wcslcpy(wchar_t *restrict arg0, const wchar_t *restrict arg1, size_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (size_t)-1; }
__attribute__((weak)) size_t wcsnrtombs(char *restrict arg0, const wchar_t **restrict arg1, size_t arg2, size_t arg3, mbstate_t *restrict arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; errno = ENOSYS; return (size_t)-1; }
__attribute__((weak)) int wprintf(const wchar_t *restrict arg0, ...) { (void)arg0; errno = ENOSYS; return (int)-1; }
__attribute__((weak)) int wscanf(const wchar_t *restrict arg0, ...) { (void)arg0; errno = ENOSYS; return (int)-1; }
