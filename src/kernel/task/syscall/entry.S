/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/syscall/entry.S
 * 系统调用入口换栈与 IRET 用户寄存器恢复。
 */

#include "kernel/cpu.h"

.section .text
.code64
.global syscallEntry
.type syscallEntry, @function
.extern syscallDispatchEntry

// 偏移直接使用与 C++ 共用的 CPU 头文件，避免两处布局不一致而把用户栈指针写进其他 CPU 字段。
.set cpuKernelStackOffset, KERNEL_CPU_STACK_OFFSET
.set cpuSyscallUserStackOffset, KERNEL_CPU_USER_STACK_OFFSET

/*
 * SYSCALL 只把用户 RIP 和标志放入 RCX、R11，不自动换栈；swapgs 后先保存用户 RSP，再切到线程内核栈，此窗口由 FMASK 屏蔽中断。
 */
syscallEntry:
    swapgs
    movq %rsp, %gs:cpuSyscallUserStackOffset
    movq %gs:cpuKernelStackOffset, %rsp

    // 额外预留两个字，返回时把三字执行位置扩成五字 IRET 帧；它们不属于十八字的 C++ 现场。
    subq $16, %rsp

    // 按 SyscallContext 的相反顺序压入用户返回信息和通用寄存器，最终最低地址是 R15，C++ 收到的指针就指向这里。
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

    // 完整现场与内核栈建立后才允许中断；进入返回帧转换和 GS 切换之前再次关中断。
    sti
    call syscallDispatchEntry
    cli

.global userContextReturn

/*
 * 普通调用、fork 和信号返回共用此入口；先用预留空间把尾部改成 RIP、CS、RFLAGS、RSP、SS，再恢复会被转换过程使用的通用寄存器。
 */
userContextReturn:
    // 先转换返回帧再恢复寄存器；SYSRET 不能完整恢复 RCX、R11 和信号返回要求的标志组合。
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

    // 依赖内核 GS 的工作已经全部结束，紧邻 IRET 才切回用户 GS，中间不能再留下可被中断的内核后续操作。
    swapgs
    iretq
.size syscallEntry, . - syscallEntry

.section .note.GNU-stack,"",@progbits
