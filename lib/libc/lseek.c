/* Public domain.  */
#include <unistd.h>

/* 占位实现 - 用于自制操作系统 */
off_t lseek(int fd, off_t offset, int whence)
{
    /* 自制操作系统中，这里应该是系统调用 */
    /* 暂时返回0作为当前位置 */
    return 0;
}