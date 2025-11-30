#include <stdio.h>
#include <stdlib.h>

/*
 * __assert_fail - Handle assertion failures
 * 
 * This is the default assertion failure handler that prints
 * diagnostic information about the failed assertion and terminates
 * the program.
 */
void __assert_fail(const char *condition, const char *file, int line, const char *function)
{
    /* Print assertion failure information */
    fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", 
            condition, file, line);
    
    if (function) {
        fprintf(stderr, "Function: %s\n", function);
    }
    
    /* In freestanding environment, we can't use abort() */
    /* but for completeness, we'll try to exit */
    exit(EXIT_FAILURE);
}