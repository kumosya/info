#ifndef _STDC_STDDEF_H
#define _STDC_STDDEF_H


typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#define offsetof(type, member) __builtin_offsetof(type, member)
#ifndef __cplusplus
#ifndef __DEFINED_wchar_t
typedef __WCHAR_TYPE__ wchar_t;
#define __DEFINED_wchar_t
#endif
#endif
#ifndef __DEFINED_max_align_t
typedef struct { long long integer; long double floating; } max_align_t;
#define __DEFINED_max_align_t
#endif

#ifndef NULL
#define NULL 0
#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_size_t
#define __DEFINED_ptrdiff_t

#endif
