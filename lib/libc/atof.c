#include <stdlib.h>
#include <ctype.h>

/*
 * atof - Convert a string to a double
 * Returns the converted value
 */
double atof(const char *nptr)
{
    double result = 0.0;
    double fraction = 0.1;
    int sign = 1;
    //int in_fraction = 0;
    
    /* Skip leading whitespace */
    while (isspace((unsigned char)*nptr)) {
        nptr++;
    }
    
    /* Check for sign */
    if (*nptr == '-' || *nptr == '+') {
        sign = (*nptr++ == '-') ? -1 : 1;
    }
    
    /* Convert integer part */
    while (isdigit((unsigned char)*nptr)) {
        result = result * 10.0 + (*nptr++ - '0');
    }
    
    /* Convert fractional part */
    if (*nptr == '.') {
        //in_fraction = 1;
        nptr++;
        
        while (isdigit((unsigned char)*nptr)) {
            result += (*nptr++ - '0') * fraction;
            fraction *= 0.1;
        }
    }
    
    /* Handle exponent */
    if (*nptr == 'e' || *nptr == 'E') {
        int exp_sign = 1;
        int exponent = 0;
        
        nptr++;
        
        /* Check exponent sign */
        if (*nptr == '-' || *nptr == '+') {
            exp_sign = (*nptr++ == '-') ? -1 : 1;
        }
        
        /* Read exponent value */
        while (isdigit((unsigned char)*nptr)) {
            exponent = exponent * 10 + (*nptr++ - '0');
        }
        
        /* Apply exponent */
        if (exp_sign > 0) {
            while (exponent--) {
                result *= 10.0;
            }
        } else {
            while (exponent--) {
                result *= 0.1;
            }
        }
    }
    
    return sign * result;
}