/* Public domain.  */
#include <stdio.h>
#include <stdlib.h>

int
fclose(FILE *stream)
{
    if (stream == NULL) {
        return EOF;
    }
    
    /* 释放缓冲区 */
    if (stream->_buf != NULL) {
        free(stream->_buf);
        stream->_buf = NULL;
    }
    
    /* 系统API暂时留作空实现 */
    
    return 0;
}