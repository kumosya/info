#include <stdlib.h>
#include <string.h>

/*
 * Declare the external environ variable
 * In a real system, this would be set by the runtime
 */
extern char **environ;

/*
 * getenv - Get value of environment variable
 * Returns a pointer to the value of the environment variable, or NULL if not found
 */
char *getenv(const char *name)
{
    int i;
    size_t len;
    
    if (name == NULL || environ == NULL) {
        return NULL;
    }
    
    len = strlen(name);
    
    /* Search through the environment variables */
    for (i = 0; environ[i] != NULL; i++) {
        /* Check if this entry starts with the name followed by '=' */
        if (strncmp(environ[i], name, len) == 0 && environ[i][len] == '=') {
            /* Return the part after the '=' */
            return environ[i] + len + 1;
        }
    }
    
    /* Variable not found */
    return NULL;
}