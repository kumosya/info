#ifndef LIBCPP_LIMITS_H
#define LIBCPP_LIMITS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Integral type limits */

/* SCHAR - signed char */
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127

/* CHAR */
#define CHAR_BIT    8
#ifndef CHAR_MIN
#define CHAR_MIN    SCHAR_MIN
#endif
#ifndef CHAR_MAX
#define CHAR_MAX    SCHAR_MAX
#endif

/* UCHAR - unsigned char */
#define UCHAR_MAX   255U

/* SHRT - short */
#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767

/* USHRT - unsigned short */
#define USHRT_MAX   65535U

/* INT */
#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     2147483647

/* UINT - unsigned int */
#define UINT_MAX    4294967295U

/* LONG */
#define LONG_MIN    (-9223372036854775807LL - 1)
#define LONG_MAX    9223372036854775807LL

/* ULONG - unsigned long */
#define ULONG_MAX   18446744073709551615ULL

/* LONG LONG */
#define LLONG_MIN   (-9223372036854775807LL - 1)
#define LLONG_MAX   9223372036854775807LL

/* ULLONG - unsigned long long */
#define ULLONG_MAX  18446744073709551615ULL

#ifdef __cplusplus
}
#endif

#endif /* LIBCPP_LIMITS_H */