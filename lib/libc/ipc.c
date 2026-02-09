#include <kernel/syscall.h>
#include <stdint.h>
#include <sys/types.h>

int msgSend(pid_t dst_pid, uint64_t type, MESSAGE *data) {
    int ret;
    __asm__ __volatile__(
        "syscall		\n"
        : "=a"(ret)
        : "a"(SYS_SEND), "D"(dst_pid), "S"(type), "d"(data)
        : "memory");

    return ret;
}

int msgRecv(pid_t *src_pid, uint64_t type, MESSAGE *msg) {
    int ret;
    __asm__ __volatile__(
        "syscall			\n"
        : "=a"(ret)
        : "a"(SYS_RECEIVE), "D"(src_pid), "S"(type), "d"(msg)
        : "memory");

    return ret;
}
