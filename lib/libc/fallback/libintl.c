/* ABI entry points for facilities without an osinfo backend. */
#define _GNU_SOURCE 1
#include <libintl.h>
#include <errno.h>
#include <unistd.h>

__attribute__((weak)) char * bind_textdomain_codeset(const char * arg0, const char * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * bindtextdomain(const char * arg0, const char * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * dcgettext(const char * arg0, const char * arg1, int arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * dcgettext_l(const char * arg0, const char * arg1, int arg2, locale_t arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * dcngettext(const char * arg0, const char * arg1, const char * arg2, unsigned long arg3, int arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * dcngettext_l(const char * arg0, const char * arg1, const char * arg2, unsigned long arg3, int arg4, locale_t arg5) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * dgettext(const char * arg0, const char * arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * dgettext_l(const char * arg0, const char * arg1, locale_t arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * dngettext(const char * arg0, const char * arg1, const char * arg2, unsigned long arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * dngettext_l(const char * arg0, const char * arg1, const char * arg2, unsigned long arg3, locale_t arg4) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; (void)arg4; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * gettext(const char * arg0) { (void)arg0; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * gettext_l(const char * arg0, locale_t arg1) { (void)arg0; (void)arg1; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * ngettext(const char * arg0, const char * arg1, unsigned long arg2) { (void)arg0; (void)arg1; (void)arg2; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * ngettext_l(const char * arg0, const char * arg1, unsigned long arg2, locale_t arg3) { (void)arg0; (void)arg1; (void)arg2; (void)arg3; errno = ENOSYS; return (char *)0; }
__attribute__((weak)) char * textdomain(const char * arg0) { (void)arg0; errno = ENOSYS; return (char *)0; }
