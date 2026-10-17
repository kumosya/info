#include <locale.h>
#include <langinfo.h>
#define LCTRANS_CUR(s) (s)
#define LCTRANS(s,c,l) (s)
#define C_LOCALE ((locale_t)0)
#define CURRENT_UTF8 1
#define __nl_langinfo_l nl_langinfo_l

hidden locale_t __libc_current_locale(void);
#define CURRENT_LOCALE __libc_current_locale()
#define UTF8_LOCALE newlocale(LC_ALL_MASK,"C.UTF-8",0)
