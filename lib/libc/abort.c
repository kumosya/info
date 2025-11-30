#include <stdlib.h>
#include <unistd.h>

/*
 * abort - Abort the current process
 * Causes an abnormal program termination with signal SIGABRT
 */
void abort(void)
{
    /* In a freestanding environment, we'll use _exit as a fallback */
    _exit(EXIT_FAILURE);
}