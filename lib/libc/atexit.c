#include <stdlib.h>

/* Maximum number of atexit handlers */
#define MAX_ATEXIT_HANDLERS 32

/* Array of atexit handlers */
static void (*atexit_handlers[MAX_ATEXIT_HANDLERS])(void);
static int num_atexit_handlers = 0;

/*
 * atexit - Register a function to be called at program termination
 * Returns 0 on success, nonzero on failure
 */
int atexit(void (*func)(void))
{
    /* Check if there's room for another handler */
    if (num_atexit_handlers >= MAX_ATEXIT_HANDLERS) {
        return -1;  /* Failure */
    }
    
    /* Register the handler */
    atexit_handlers[num_atexit_handlers++] = func;
    
    return 0;  /* Success */
}