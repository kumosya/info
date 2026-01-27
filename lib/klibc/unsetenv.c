#include <stdlib.h>
#include <string.h>

/*
 * Declare the external environ variable
 */
extern char **environ;

/*
 * Simple implementation of unsetenv
 * In a real implementation, this would be more complex to handle dynamic environment
 */
int unsetenv(const char *name)
{
    int i, j;
    size_t len;
    
    if (name == NULL || *name == '\0' || strchr(name, '=') != NULL) {
        return -1;  /* Invalid arguments */
    }
    
    len = strlen(name);
    
    /* Search for the variable to remove */
    for (i = 0; environ[i] != NULL; i++) {
        if (strncmp(environ[i], name, len) == 0 && environ[i][len] == '=') {
            /* In a real implementation, we would free the memory */
            /* and shift the remaining entries */
            for (j = i; environ[j] != NULL; j++) {
                environ[j] = environ[j + 1];
            }
            break;
        }
    }
    
    return 0;  /* Success (even if variable wasn't found) */
}