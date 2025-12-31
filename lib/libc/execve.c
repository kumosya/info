/* Public domain.  */
#include <unistd.h>
#include <kernel/syscall.h>


int execve(const char *filename, char *const argv[], char *const envp[])
{
    int ret;
    __asm__	__volatile__	("movq   %%rdx, %%r8\n"
                    "leaq	execve_ret(%%rip),	%%rdx	\n"
					"movq	%%rsp,	%%rcx		\n"
					"sysenter			\n"
					"execve_ret:	\n"
					:"=a"(ret):"a"(SYS_EXECVE_NO), "D"(filename), "S"(argv), "d"(envp):"memory");
    
    return ret;
}