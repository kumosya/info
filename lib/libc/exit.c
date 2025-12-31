#include <stdlib.h>
#include <unistd.h>
#include "kernel/syscall.h"

/* Maximum number of atexit handlers */
#define MAX_ATEXIT_HANDLERS 32

/* Array of atexit handlers */
static void (*atexit_handlers[MAX_ATEXIT_HANDLERS])(void);
static int num_atexit_handlers = 0;

int _exit(int status) {
    int ret;
    __asm__	__volatile__	(	"leaq	__exit_ret(%%rip),	%%rdx	\n"
					"movq	%%rsp,	%%rcx		\n"
					"sysenter			\n"
					"__exit_ret:	\n"
					:"=a"(ret):"a"(SYS_EXIT_NO), "D"(status):"memory");
    
    return ret;
}

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
/*
 * abort - Abort the current process
 * Causes an abnormal program termination with signal SIGABRT
 */
void abort(void)
{
    /* In a freestanding environment, we'll use _exit as a fallback */
    _exit(EXIT_FAILURE);
}
