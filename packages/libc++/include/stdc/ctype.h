#ifndef _STDC_CTYPE_H
#define _STDC_CTYPE_H


#ifdef __cplusplus
extern "C" {
#endif
int isalnum(int character);
int isalpha(int character);
int isdigit(int character);
int isspace(int character);
int isprint(int character);
int isblank(int character);
int iscntrl(int character);
int isgraph(int character);
int islower(int character);
int ispunct(int character);
int isupper(int character);
int isxdigit(int character);
int tolower(int character);
int toupper(int character);
#ifdef __cplusplus
}
#endif

/* POSIX.1-2024 declarations. */
#include <features.h>
#include <locale.h>
#ifdef __cplusplus
extern "C" {
#endif
int isalnum_l(int, locale_t);
int isalpha_l(int, locale_t);
int isblank_l(int, locale_t);
int iscntrl_l(int, locale_t);
int isdigit_l(int, locale_t);
int isgraph_l(int, locale_t);
int islower_l(int, locale_t);
int isprint_l(int, locale_t);
int ispunct_l(int, locale_t);
int isspace_l(int, locale_t);
int isupper_l(int, locale_t);
int isxdigit_l(int, locale_t);
int tolower_l(int, locale_t);
int toupper_l(int, locale_t);
#ifdef __cplusplus
}
#endif

#endif
