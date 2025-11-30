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