#include <stdlib.h>

/*
 * free - Free allocated memory
 * Releases the memory block pointed to by ptr
 */
void free(void *ptr)
{
    /* In a real implementation, we would also want to coalesce adjacent free blocks */
    /* This simple implementation doesn't do that */
}