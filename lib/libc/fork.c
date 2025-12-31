/* Public domain.  */
#include <unistd.h>
#include <kernel/syscall.h>

pid_t fork(void)
{
    pid_t ret;
    __asm__	__volatile__	(	"leaq	fork_ret(%%rip),	%%rdx	\n"
					"movq	%%rsp,	%%rcx		\n"
					"sysenter			\n"
					"fork_ret:	\n"
					:"=a"(ret):"a"(SYS_FORK_NO):"memory");
    
    return ret;
}