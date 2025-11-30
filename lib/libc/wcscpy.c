#include <wchar.h>

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