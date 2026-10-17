#include <os/syscall.h>

.section .rodata.user_probe,"a"
.code64
.global userSyscallProbeStart
.global userSyscallProbeEnd
userSyscallProbeStart:
    movq $OS_SYS_MAP_MEMORY, %rax
    movq $0x900000, %rdi
    movq $4096, %rsi
    movq $1, %rdx
    syscall
    cmpq $0x900000, %rax
    jne 1f
    movabsq $0x6f73696e666f, %rbx
    movq %rbx, (%rax)

    movq $OS_SYS_UNMAP_MEMORY, %rax
    movq $0x900000, %rdi
    movq $4096, %rsi
    syscall
    testq %rax, %rax
    jne 1f

    movq $OS_SYS_YIELD, %rax
    syscall
    movq $OS_SYS_EXIT_TASK, %rax
    xorq %rdi, %rdi
    syscall
1:
    movq $OS_SYS_EXIT_TASK, %rax
    movq $1, %rdi
    syscall
    ud2
userSyscallProbeEnd:

.section .note.GNU-stack,"",@progbits
