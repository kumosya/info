/* Public domain.  */
#include <stdio.h>

int
ferror(FILE *stream)
{
    return stream != NULL ? stream->_error : 0;
}