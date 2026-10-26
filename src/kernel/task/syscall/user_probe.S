#include <os/syscall.h>
#include "service/common/entry.h"
.section .rodata.user_probe,"a"
.code64
.global userSyscallProbeStart
.global userSyscallProbeEnd
userSyscallProbeStart:
    movq %rdi, %r12
    subq $8448, %rsp
    movq %rsp, %r13
    leaq 4224(%rsp), %r14
    movq %rsp, %rdi
    movq $1056, %rcx
    xorq %rax, %rax
    rep stosq
    movq $OS_TASK_YIELD_OPERATION, (%r13)
    movl $5, 32(%r13)
    movq $OS_SYS_CALL_SERVICE, %rax
    movq %r12, %rdi
    movq %r13, %rsi
    movq %r14, %rdx
    movq $-1, %r10
    syscall
    movq $OS_TASK_EXIT_OPERATION, (%r13)
    movq $OS_SYS_CALL_SERVICE, %rax
    movq %r12, %rdi
    movq %r13, %rsi
    movq %r14, %rdx
    movq $-1, %r10
    syscall
    ud2
userSyscallProbeEnd:
.section .note.GNU-stack,"",@progbits
