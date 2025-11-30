/* Minimal <stdint.h> for x86_64 freestanding kernel.
   Provides fixed-width integer types and common limits.
   This is intentionally small — adjust if you need extra typedefs/macros. */

#ifndef LIBC_STDINT_H
#define LIBC_STDINT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Exact-width types (use compiler builtin typedefs). */
typedef __INT8_TYPE__    int8_t;
typedef __UINT8_TYPE__   uint8_t;
typedef __INT16_TYPE__   int16_t;
typedef __UINT16_TYPE__  uint16_t;
typedef __INT32_TYPE__   int32_t;
typedef __UINT32_TYPE__  uint32_t;
typedef __INT64_TYPE__   int64_t;
typedef __UINT64_TYPE__  uint64_t;

/* Fast/least aliases (map to exact-width for simplicity). */
typedef int8_t   int_least8_t;
typedef uint8_t  uint_least8_t;
typedef int16_t  int_least16_t;
typedef uint16_t uint_least16_t;
typedef int32_t  int_least32_t;
typedef uint32_t uint_least32_t;
typedef int64_t  int_least64_t;
typedef uint64_t uint_least64_t;

typedef int8_t   int_fast8_t;
typedef uint8_t  uint_fast8_t;
typedef int32_t  int_fast32_t;
typedef uint32_t uint_fast32_t;
typedef int64_t  int_fast64_t;
typedef uint64_t uint_fast64_t;

/* Pointer-sized integer types */
typedef __INTPTR_TYPE__  intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;

/* Greatest-width integer types */
typedef __INTMAX_TYPE__  intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;

/* Limits for exact-width types */
#define INT8_MIN   (-128)
#define INT8_MAX   (127)
#define UINT8_MAX  (255U)

#define INT16_MIN  (-32768)
#define INT16_MAX  (32767)
#define UINT16_MAX (65535U)

#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX  (2147483647)
#define UINT32_MAX (4294967295U)

#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX  (9223372036854775807LL)
#define UINT64_MAX (18446744073709551615ULL)

/* Limits for pointer-sized integers (assume 64-bit environment here) */
#define INTPTR_MIN  INT64_MIN
#define INTPTR_MAX  INT64_MAX
#define UINTPTR_MAX UINT64_MAX

/* Limits for greatest-width integers */
#define INTMAX_MIN  INT64_MIN
#define INTMAX_MAX  INT64_MAX
#define UINTMAX_MAX UINT64_MAX

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBC_STDINT_H */
