#include <wchar.h>

/*
 * wcscmp - Compare wide strings
 * Compares the wide string pointed to by s1 to the wide string pointed to by s2
 * Returns an integer less than, equal to, or greater than zero if s1 is found,
 * respectively, to be less than, to match, or be greater than s2.
 */
int wcscmp(const wchar_t *s1, const wchar_t *s2)
{
    while (*s1 && *s2 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    
    return (int)(*s1 - *s2);
}

/*
 * wcscpy - Copy wide string
 * Copies the wide string pointed to by s2 (including the terminating null wide character)
 * to the array pointed to by s1.
 */
wchar_t *wcscpy(wchar_t *restrict s1, const wchar_t *restrict s2)
{
    wchar_t *dest = s1;
    
    while ((*dest++ = *s2++) != L'\0') {
        /* Copy until null terminator is reached */
    }
    
    return s1;
}
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