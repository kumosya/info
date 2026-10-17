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
    popq %rcx
    popq %r11
    movq (%rsp), %rsp
    swapgs
    sysretq
.size syscallEntry, . - syscallEntry

.section .note.GNU-stack,"",@progbits
