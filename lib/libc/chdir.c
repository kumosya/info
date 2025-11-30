/* Public domain.  */
#include <unistd.h>

/* 占位实现 - 用于自制操作系统 */
int chdir(const char *path)
{
    /* 自制操作系统中，这里应该是系统调用 */
    /* 暂时返回0表示成功 */
    return 0;
}