#include <stdlib.h>

/*
 * div - Integer division with quotient and remainder
 * Returns a structure containing the quotient and remainder
 */
div_t div(int numerator, int denominator)
{
    div_t result;
    
    if (denominator == 0) {
        /* Division by zero - behavior is undefined, but we'll return 0 */
        result.quot = 0;
        result.rem = 0;
        return result;
    }
    
    result.quot = numerator / denominator;
    result.rem = numerator % denominator;
    
    /* Ensure remainder has the same sign as the numerator */
    if (result.rem < 0 && numerator > 0) {
        result.rem += denominator;
        result.quot--;
    } else if (result.rem > 0 && numerator < 0) {
        result.rem -= denominator;
        result.quot++;
    }
    
    return result;
}

/*
 * ldiv - Long integer division with quotient and remainder
 * Returns a structure containing the quotient and remainder
 */
ldiv_t ldiv(long numerator, long denominator)
{
    ldiv_t result;
    
    if (denominator == 0) {
        /* Division by zero - behavior is undefined, but we'll return 0 */
        result.quot = 0;
        result.rem = 0;
        return result;
    }
    
    result.quot = numerator / denominator;
    result.rem = numerator % denominator;
    
    /* Ensure remainder has the same sign as the numerator */
    if (result.rem < 0 && numerator > 0) {
        result.rem += denominator;
        result.quot--;
    } else if (result.rem > 0 && numerator < 0) {
        result.rem -= denominator;
        result.quot++;
    }
    
    return result;
}

/*
 * lldiv - Long long integer division with quotient and remainder
 * Returns a structure containing the quotient and remainder
 */
lldiv_t lldiv(long long numerator, long long denominator)
{
    lldiv_t result;
    
    if (denominator == 0) {
        /* Division by zero - behavior is undefined, but we'll return 0 */
        result.quot = 0;
        result.rem = 0;
        return result;
    }
    
    result.quot = numerator / denominator;
    result.rem = numerator % denominator;
    
    /* Ensure remainder has the same sign as the numerator */
    if (result.rem < 0 && numerator > 0) {
        result.rem += denominator;
        result.quot--;
    } else if (result.rem > 0 && numerator < 0) {
        result.rem -= denominator;
        result.quot++;
    }
    
    return result;
}