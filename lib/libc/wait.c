/* Public domain.  */
#include <sys/types.h>
#include <sys/wait.h>

/* 占位实现 - 用于自制操作系统 */
pid_t wait(int *status)
{
    /* 自制操作系统中，这里应该是系统调用 */
    /* 暂时返回-1表示错误 */
    return -1;
}