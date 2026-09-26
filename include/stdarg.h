#pragma once

typedef __builtin_va_list va_list;
#define va_start(list, last) __builtin_va_start(list, last)
#define va_end(list) __builtin_va_end(list)
#define va_arg(list, type) __builtin_va_arg(list, type)
#define va_copy(destination, source) __builtin_va_copy(destination, source)
