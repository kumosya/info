#include <wchar.h>

/*
 * wcslen - Get length of wide string
 * Returns the number of wide characters in the string before the terminating null wide character
 */
size_t wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    
    while (*p != L'\0') {
        p++;
    }
    
    return (size_t)(p - s);
}