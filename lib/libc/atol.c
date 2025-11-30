#include <stdlib.h>
#include <ctype.h>

/*
 * atol - Convert a string to a long integer
 * Returns the converted value
 */
long atol(const char *nptr)
{
    long result = 0;
    int sign = 1;
    
    /* Skip leading whitespace */
    while (isspace((unsigned char)*nptr)) {
        nptr++;
    }
    
    /* Check for sign */
    if (*nptr == '-' || *nptr == '+') {
        sign = (*nptr++ == '-') ? -1 : 1;
    }
    
    /* Convert digits */
    while (isdigit((unsigned char)*nptr)) {
        result = result * 10 + (*nptr++ - '0');
    }
    
    return sign * result;
}