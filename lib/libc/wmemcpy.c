#include <wchar.h>

/*
 * wmemcpy - Copy wide characters in memory
 * Copies n wide characters from the object pointed to by s2 to the object pointed to by s1.
 * The objects are interpreted as arrays of unsigned wchar_t.
 */
void *wmemcpy(void *restrict s1, const void *restrict s2, size_t n)
{
    wchar_t *d = (wchar_t *)s1;
    const wchar_t *s = (const wchar_t *)s2;
    
    while (n-- > 0) {
        *d++ = *s++;
    }
    
    return s1;
}