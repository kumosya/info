#include <stdlib.h>
#include <unistd.h>

/* Maximum number of atexit handlers */
#define MAX_ATEXIT_HANDLERS 32

/* Array of atexit handlers */
static void (*atexit_handlers[MAX_ATEXIT_HANDLERS])(void);
static int num_atexit_handlers = 0;

/*
 * exit - Terminate the calling process
 * Calls all functions registered with atexit in reverse order,
 * then terminates the process with the specified status
 */
void exit(int status)
{
    int i;
    
    /* Call all atexit handlers in reverse order */
    for (i = num_atexit_handlers - 1; i >= 0; i--) {
        if (atexit_handlers[i] != NULL) {
            atexit_handlers[i]();
        }
    }
    
    /* Terminate the process */
    _exit(status);
}