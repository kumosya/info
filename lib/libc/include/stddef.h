#ifndef LIBC_STDDEF_H
#define LIBC_STDDEF_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NULL
#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif
#endif

typedef long ssize_t;
typedef unsigned long size_t;

#if ((defined(__GNUC__) && (__GNUC__ >= 4)) || defined(__clang__))
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({ \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type,member) ); \
})

#ifdef __cplusplus
}
#endif

#endif /* LIBC_STDDEF_H */
