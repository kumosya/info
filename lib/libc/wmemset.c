#include <wchar.h>

/*
 * wmemset - Fill memory with wide character
 * Copies the wide character c (converted to an unsigned type) into each of the first n
 * wide characters of the object pointed to by s.
 */
void *wmemset(void *s, wchar_t c, size_t n)
{
    wchar_t *p = (wchar_t *)s;
    
    while (n-- > 0) {
        *p++ = c;
    }
    
    return s;
}