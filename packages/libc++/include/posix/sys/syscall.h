#ifndef _POSIX_SYS_SYSCALL_H
#define _POSIX_SYS_SYSCALL_H

#include <os/syscall.h>

/* Native entries only. Process and VM operations use the libc interfaces. */
static inline long os_syscall(long number, long a, long b, long c, long d,
                              long e, long f) {
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8")   = e;
    register long r9 __asm__("r9")   = f;
    long result;
    unsigned short cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    if ((cs & 3) != 3) return -11;
    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8),
                       "r"(r9)
                     : "rcx", "r11", "memory", "cc");
    return result;
}

#endif
