/* Public domain.  */
#include <unistd.h>

/* 占位实现 - 用于自制操作系统 */
ssize_t write(int fd, const void *buf, size_t count)
{
    /* 自制操作系统中，这里应该是系统调用 */
    /* 暂时返回请求的字节数，表示全部写入成功 */
    return count;
}