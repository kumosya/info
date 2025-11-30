/* Public domain.  */
#include <fcntl.h>

/* 占位实现 - 用于自制操作系统 */
int open(const char *path, int oflag, ...)
{
    /* 自制操作系统中，这里应该是系统调用 */
    /* 暂时返回错误 */
    return -1;
}

/* creat是open的特例，使用O_WRONLY|O_CREAT|O_TRUNC标志 */
int creat(const char *path, mode_t mode)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}