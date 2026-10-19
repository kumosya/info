#ifndef _STDC_STDATOMIC_H
#define _STDC_STDATOMIC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST
} memory_order;

#ifdef __cplusplus
#define _Atomic(T) T
#else
#define _Atomic(T) _Atomic T
#endif
#define ATOMIC_VAR_INIT(value) (value)
#define kill_dependency(value) (value)
#define ATOMIC_BOOL_LOCK_FREE __GCC_ATOMIC_BOOL_LOCK_FREE
#define ATOMIC_CHAR_LOCK_FREE __GCC_ATOMIC_CHAR_LOCK_FREE
#define ATOMIC_CHAR16_T_LOCK_FREE __GCC_ATOMIC_CHAR16_T_LOCK_FREE
#define ATOMIC_CHAR32_T_LOCK_FREE __GCC_ATOMIC_CHAR32_T_LOCK_FREE
#define ATOMIC_WCHAR_T_LOCK_FREE __GCC_ATOMIC_WCHAR_T_LOCK_FREE
#define ATOMIC_SHORT_LOCK_FREE __GCC_ATOMIC_SHORT_LOCK_FREE
#define ATOMIC_INT_LOCK_FREE __GCC_ATOMIC_INT_LOCK_FREE
#define ATOMIC_LONG_LOCK_FREE __GCC_ATOMIC_LONG_LOCK_FREE
#define ATOMIC_LLONG_LOCK_FREE __GCC_ATOMIC_LLONG_LOCK_FREE
#define ATOMIC_POINTER_LOCK_FREE __GCC_ATOMIC_POINTER_LOCK_FREE

typedef _Atomic(bool) atomic_bool;
typedef _Atomic(char) atomic_char;
typedef _Atomic(signed char) atomic_schar;
typedef _Atomic(unsigned char) atomic_uchar;
typedef _Atomic(short) atomic_short;
typedef _Atomic(unsigned short) atomic_ushort;
typedef _Atomic(int) atomic_int;
typedef _Atomic(unsigned) atomic_uint;
typedef _Atomic(long) atomic_long;
typedef _Atomic(unsigned long) atomic_ulong;
typedef _Atomic(long long) atomic_llong;
typedef _Atomic(unsigned long long) atomic_ullong;
typedef _Atomic(uint_least16_t) atomic_char16_t;
typedef _Atomic(uint_least32_t) atomic_char32_t;
typedef _Atomic(__WCHAR_TYPE__) atomic_wchar_t;
typedef _Atomic(intptr_t) atomic_intptr_t;
typedef _Atomic(uintptr_t) atomic_uintptr_t;
typedef _Atomic(size_t) atomic_size_t;
typedef _Atomic(ptrdiff_t) atomic_ptrdiff_t;
typedef _Atomic(intmax_t) atomic_intmax_t;
typedef _Atomic(uintmax_t) atomic_uintmax_t;
#define __ATOMIC_INTEGER_TYPES(bits) \
    typedef _Atomic(int_least##bits##_t) atomic_int_least##bits##_t; \
    typedef _Atomic(uint_least##bits##_t) atomic_uint_least##bits##_t; \
    typedef _Atomic(int_fast##bits##_t) atomic_int_fast##bits##_t; \
    typedef _Atomic(uint_fast##bits##_t) atomic_uint_fast##bits##_t;
__ATOMIC_INTEGER_TYPES(8)
__ATOMIC_INTEGER_TYPES(16)
__ATOMIC_INTEGER_TYPES(32)
__ATOMIC_INTEGER_TYPES(64)
#undef __ATOMIC_INTEGER_TYPES

typedef struct { atomic_bool value; } atomic_flag;
#define ATOMIC_FLAG_INIT {false}
#define atomic_init(p,v) (*(p) = (v))
#define atomic_is_lock_free(p) __atomic_is_lock_free(sizeof(*(p)), (p))
#define atomic_store_explicit(p,v,m) __atomic_store_n((p),(v),(m))
#define atomic_store(p,v) atomic_store_explicit((p),(v),memory_order_seq_cst)
#define atomic_load_explicit(p,m) __atomic_load_n((p),(m))
#define atomic_load(p) atomic_load_explicit((p),memory_order_seq_cst)
#define atomic_exchange_explicit(p,v,m) __atomic_exchange_n((p),(v),(m))
#define atomic_exchange(p,v) atomic_exchange_explicit((p),(v),memory_order_seq_cst)
#define atomic_compare_exchange_strong_explicit(p,e,v,s,f) __atomic_compare_exchange_n((p),(e),(v),false,(s),(f))
#define atomic_compare_exchange_weak_explicit(p,e,v,s,f) __atomic_compare_exchange_n((p),(e),(v),true,(s),(f))
#define atomic_compare_exchange_strong(p,e,v) atomic_compare_exchange_strong_explicit((p),(e),(v),memory_order_seq_cst,memory_order_seq_cst)
#define atomic_compare_exchange_weak(p,e,v) atomic_compare_exchange_weak_explicit((p),(e),(v),memory_order_seq_cst,memory_order_seq_cst)
#define atomic_fetch_add_explicit(p,v,m) __atomic_fetch_add((p),(v),(m))
#define atomic_fetch_sub_explicit(p,v,m) __atomic_fetch_sub((p),(v),(m))
#define atomic_fetch_or_explicit(p,v,m) __atomic_fetch_or((p),(v),(m))
#define atomic_fetch_xor_explicit(p,v,m) __atomic_fetch_xor((p),(v),(m))
#define atomic_fetch_and_explicit(p,v,m) __atomic_fetch_and((p),(v),(m))
#define atomic_fetch_add(p,v) atomic_fetch_add_explicit((p),(v),memory_order_seq_cst)
#define atomic_fetch_sub(p,v) atomic_fetch_sub_explicit((p),(v),memory_order_seq_cst)
#define atomic_fetch_or(p,v) atomic_fetch_or_explicit((p),(v),memory_order_seq_cst)
#define atomic_fetch_xor(p,v) atomic_fetch_xor_explicit((p),(v),memory_order_seq_cst)
#define atomic_fetch_and(p,v) atomic_fetch_and_explicit((p),(v),memory_order_seq_cst)
#define atomic_thread_fence(m) __atomic_thread_fence(m)
#define atomic_signal_fence(m) __atomic_signal_fence(m)
#define atomic_flag_test_and_set_explicit(p,m) __atomic_test_and_set(&(p)->value,(m))
#define atomic_flag_test_and_set(p) atomic_flag_test_and_set_explicit((p),memory_order_seq_cst)
#define atomic_flag_clear_explicit(p,m) __atomic_clear(&(p)->value,(m))
#define atomic_flag_clear(p) atomic_flag_clear_explicit((p),memory_order_seq_cst)

#endif

/* __OSINFO_TYPE_GUARDS: shared type requests. */
#define __DEFINED_atomic_bool
#define __DEFINED_atomic_char
#define __DEFINED_atomic_schar
#define __DEFINED_atomic_uchar
#define __DEFINED_atomic_short
#define __DEFINED_atomic_ushort
#define __DEFINED_atomic_int
#define __DEFINED_atomic_uint
#define __DEFINED_atomic_long
#define __DEFINED_atomic_ulong
#define __DEFINED_atomic_llong
#define __DEFINED_atomic_ullong
#define __DEFINED_atomic_char16_t
#define __DEFINED_atomic_char32_t
#define __DEFINED_atomic_wchar_t
#define __DEFINED_atomic_intptr_t
#define __DEFINED_atomic_uintptr_t
#define __DEFINED_atomic_size_t
#define __DEFINED_atomic_ptrdiff_t
#define __DEFINED_atomic_intmax_t
#define __DEFINED_atomic_uintmax_t
#define __DEFINED_atomic_flag
