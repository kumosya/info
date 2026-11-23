#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include "stdio_internal.h"

/* 有限宽字符串直接作为流后端，不为超大的字段宽度分配无限临时输出。 */
int vswprintf(wchar_t* restrict output, size_t size,
                 const wchar_t* restrict format, va_list arguments)
{
    if (!size) return -1;
    if (!output || !format || size > SIZE_MAX / sizeof(wchar_t)) { errno = EINVAL; return -1; }
    FILE* stream = __libc_wide_string_stream(output, size, 0);
    if (!stream) return -1;
    int result = vfwprintf(stream, format, arguments);
    int closed = fclose(stream);
    return closed || result < 0 || (size_t)result >= size ? -1 : result;
}

/* 输入宽字符串直接供扫描器读取，避免先转成本地区域编码造成字符丢失。 */
int vswscanf(const wchar_t* restrict input, const wchar_t* restrict format, va_list arguments)
{
    if (!input || !format) { errno = EINVAL; return EOF; }
    FILE* stream = __libc_wide_string_stream((wchar_t*)input, wcslen(input), 1);
    if (!stream) return EOF;
    int result = vfwscanf(stream, format, arguments);
    fclose(stream);
    return result;
}

int fwprintf(FILE* restrict stream, const wchar_t* restrict format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfwprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int swprintf(wchar_t* restrict output, size_t size, const wchar_t* restrict format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vswprintf(output, size, format, arguments);
    va_end(arguments);
    return result;
}

int vwprintf(const wchar_t* restrict format, va_list arguments)
{
    return vfwprintf(stdout, format, arguments);
}

int wprintf(const wchar_t* restrict format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vwprintf(format, arguments);
    va_end(arguments);
    return result;
}

int fwscanf(FILE* restrict stream, const wchar_t* restrict format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfwscanf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int swscanf(const wchar_t* restrict input, const wchar_t* restrict format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vswscanf(input, format, arguments);
    va_end(arguments);
    return result;
}

int vwscanf(const wchar_t* restrict format, va_list arguments)
{
    return vfwscanf(stdin, format, arguments);
}

int wscanf(const wchar_t* restrict format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vwscanf(format, arguments);
    va_end(arguments);
    return result;
}
