/* Public domain.  */
#include <stdio.h>
#include <stdlib.h>

FILE *
fopen(const char *filename, const char *mode)
{
    FILE *stream;
    
    if (filename == NULL || mode == NULL) {
        return NULL;
    }
    
    /* 分配FILE结构 */
    stream = (FILE *)malloc(sizeof(FILE));
    if (stream == NULL) {
        return NULL;
    }
    
    /* 初始化FILE结构 */
    stream->_buf = NULL;
    stream->_fd = -1;
    stream->_size = 0;
    stream->_len = 0;
    stream->_flags = 0;
    stream->_error = 0;
    stream->_eof = 0;
    
    /* 系统API暂时留作空实现 */
    
    return stream;
}