#include <stdlib.h>
#include <wchar.h>

/*
 * mbstowcs - Convert a multibyte string to a wide string
 * Returns the number of wide characters converted, or (size_t)-1 on error
 * This is a minimal implementation that assumes ASCII encoding
 */
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n)
{
    size_t count = 0;
    
    if (pwcs == NULL) {
        /* Just count the number of wide characters needed */
        while (*s != '\0') {
            count++;
            s++;
        }
        return count;
    }
    
    /* Convert up to n-1 characters, leaving room for the null terminator */
    while (*s != '\0' && count < n) {
        pwcs[count] = (wchar_t)*s;
        count++;
        s++;
    }
    
    /* Add the null terminator if there's room */
    if (count < n) {
        pwcs[count] = L'\0';
    }
    
    return count;
}

/*
 * wcstombs - Convert a wide string to a multibyte string
 * Returns the number of bytes converted, or (size_t)-1 on error
 * This is a minimal implementation that assumes ASCII encoding
 */
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n)
{
    size_t count = 0;
    
    if (s == NULL) {
        /* Just count the number of bytes needed */
        while (*pwcs != L'\0') {
            count++;
            pwcs++;
        }
        return count;
    }
    
    /* Convert up to n-1 bytes, leaving room for the null terminator */
    while (*pwcs != L'\0' && count < n) {
        s[count] = (char)*pwcs;
        count++;
        pwcs++;
    }
    
    /* Add the null terminator if there's room */
    if (count < n) {
        s[count] = '\0';
    }
    
    return count;
}