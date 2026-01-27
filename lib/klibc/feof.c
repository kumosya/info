/* Public domain.  */
#include <stdio.h>

int
feof(FILE *stream)
{
    return stream != NULL ? stream->_eof : 0;
}