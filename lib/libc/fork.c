/* Public domain.  */
#include <unistd.h>

/* 占位实现 - 用于自制操作系统 */
pid_t fork(void)
{
    /* 自制操作系统中，这里应该是系统调用 */
    /* 暂时返回-1表示错误 */
    return -1;
}