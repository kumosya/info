/* Public domain.  */
#include <sys/types.h>
#include <sys/wait.h>
#include "kernel/syscall.h"

pid_t wait(int *status)
{
    pid_t ret;
    __asm__	__volatile__	(	"leaq	wait_ret(%%rip),	%%rdx	\n"
					"movq	%%rsp,	%%rcx		\n"
					"sysenter			\n"
					"wait_ret:	\n"
					:"=a"(ret):"a"(SYS_WAIT_NO), "D"(status):"memory");
    
    return ret;
}