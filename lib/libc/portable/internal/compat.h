#pragma once
#include <features.h>
#define hidden __attribute__((visibility("hidden")))
#define weak __attribute__((weak))
#define weak_alias(old, new) \
    extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
#include <stddef.h>
hidden char* __strchrnul(const char*, int);
hidden char* __stpcpy(char* restrict, const char* restrict);
hidden char* __stpncpy(char* restrict, const char* restrict, size_t);
struct in_addr;
hidden int __inet_aton(const char*, struct in_addr*);
hidden char* __randname(char*);
hidden int __mkostemps(char*, int, int);
