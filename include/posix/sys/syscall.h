#ifndef _POSIX_SYS_SYSCALL_H
#define _POSIX_SYS_SYSCALL_H

#include <os/syscall.h>

/* Compatibility names map to OS operations with the same argument layout. */
#define SYS_exit OS_SYS_EXIT_TASK
#define SYS_exit_group OS_SYS_EXIT_TASK
#define SYS_sched_yield OS_SYS_YIELD
#define SYS_fork OS_SYS_FORK_TASK
#define SYS_munmap OS_SYS_UNMAP_MEMORY
#define SYS_mprotect OS_SYS_PROTECT_MEMORY

static inline long os_syscall(long number, long a, long b, long c,
                              long d, long e, long f) {
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    long result;
    unsigned short cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    if ((cs & 3) != 3) return -11;
    __asm__ volatile("syscall" : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c),
                       "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory", "cc");
    return result;
}

#endif
