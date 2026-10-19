/* Filename pattern matching. */
#ifndef _POSIX_FNMATCH_H
#define _POSIX_FNMATCH_H
#define FNM_PATHNAME 1
#define FNM_NOESCAPE 2
#define FNM_PERIOD 4
#define FNM_NOMATCH 1
#ifdef __cplusplus
extern "C" {
#endif
int fnmatch(const char*, const char*, int);
#ifdef __cplusplus
}
#endif
#endif

