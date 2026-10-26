#include "kernel/cpu.h"

.section .text
.code64
.global syscallEntry
.type syscallEntry, @function
.extern syscallDispatchEntry

.set cpuKernelStackOffset, KERNEL_CPU_STACK_OFFSET
.set cpuSyscallUserStackOffset, KERNEL_CPU_USER_STACK_OFFSET

syscallEntry:
    swapgs
    movq %rsp, %gs:cpuSyscallUserStackOffset
    movq %gs:cpuKernelStackOffset, %rsp
    subq $16, %rsp

    pushq %gs:cpuSyscallUserStackOffset
    pushq %r11
    pushq %rcx
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    sti
    call syscallDispatchEntry
    cli

.global userContextReturn
userContextReturn:
    # Convert the native three-word tail to an IRET frame before restoring GPRs.
    # SYSRET cannot restore RCX/R11 or all signal-return flag combinations.
    movq 128(%rsp), %rax
    movq 136(%rsp), %rcx
    movq $0x23, 128(%rsp)
    movq %rax, 136(%rsp)
    movq %rcx, 144(%rsp)
    movq $0x1b, 152(%rsp)
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    swapgs
    iretq
.size syscallEntry, . - syscallEntry

.section .note.GNU-stack,"",@progbits
