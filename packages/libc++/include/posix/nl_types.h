#ifndef _POSIX_NL_TYPES_H
#define _POSIX_NL_TYPES_H
#ifndef _NL_TYPES_H
#define _NL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#define NL_SETD 1
#define NL_CAT_LOCALE 1

typedef int nl_item;
typedef void *nl_catd;

nl_catd catopen (const char *, int);
char *catgets (nl_catd, int, int, const char *);
int catclose (nl_catd);

#ifdef __cplusplus
}
#endif

#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_nl_item
#define __DEFINED_nl_catd

#endif
