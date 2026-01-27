/* Public domain.  */
#include <stdio.h>

void
clearerr(FILE *stream)
{
    if (stream != NULL) {
        stream->_error = 0;
        stream->_eof = 0;
    }
}