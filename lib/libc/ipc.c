#include <kernel/syscall.h>
#include <stdint.h>
#include <sys/types.h>

int msgSend(pid_t dst_pid, uint64_t type, MESSAGE *data) {
    int ret;
    __asm__ __volatile__(
        "movq   %4, %%r8        \n"
        "leaq	__send_ret(%%rip),	%%rdx	\n"
        "movq	%%rsp,	%%rcx		\n"
        "sysenter			\n"
        "__send_ret:	\n"
        : "=a"(ret)
        : "a"(SYS_SEND), "D"(dst_pid), "S"(type), "r"(data)
        : "memory");

    return ret;
}

int msgRecv(pid_t *src_pid, uint64_t type, MESSAGE *msg) {
    int ret;
    __asm__ __volatile__(
        "movq   %4, %%r8        \n"
        "leaq	__recv_ret(%%rip),	%%rdx	\n"
        "movq	%%rsp,	%%rcx		\n"
        "sysenter			\n"
        "__recv_ret:	\n"
        : "=a"(ret)
        : "a"(SYS_RECEIVE), "D"(src_pid), "S"(type), "r"(msg)
        : "memory");

    return ret;
}
