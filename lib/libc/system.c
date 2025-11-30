#include <stdlib.h>
#include <unistd.h>

/*
 * system - Execute a shell command
 * Returns the status of the child process or -1 on error
 * In a freestanding environment, this is a minimal implementation
 */
int system(const char *command)
{
    int status = 0;
    
    if (command == NULL) {
        /* Check if a command processor is available */
        /* In a freestanding environment, we'll return 1 to indicate availability */
        return 1;
    }
    
    /* In a real implementation, we would fork a child process and execute the command */
    /* For now, we'll just return 0 as a placeholder */
    
    return status;
}