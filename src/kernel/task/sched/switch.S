/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sched/switch.S
 * Kernel stack switching and initial or resumed user execution.
 */

.section .text
.code64
.global switchThreadContext
.type switchThreadContext, @function
// RDI points to the outgoing saved RSP slot; RSI is the incoming saved RSP.
// Save flags and all fifteen GPRs on the outgoing kernel stack before
// publishing its RSP. The incoming stack has exactly the inverse layout,
// including either a suspended return address or Thread::bootstrap.
switchThreadContext:
    pushfq
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
    // Registers are already saved, so RDI/RSI can now select the two stacks.
    // After this assignment the pops read another thread's frame; no outgoing
    // stack-local reference may be used until this thread is scheduled again.
    movq %rsp, (%rdi)
    movq %rsi, %rsp
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
    popfq
    ret
.size switchThreadContext, . - switchThreadContext

.global enterUserMode
.type enterUserMode, @function
// RDI = user entry, RSI = user stack, RDX = initial argument. With interrupts
// disabled, build SS/RSP/RFLAGS/CS/RIP for Ring3 and move the argument to RDI.
// swapgs must happen only after kernel GS-relative work is finished; IRET then
// changes both privilege level and stack, enabling IF from the frame.
enterUserMode:
    cli
    movw $0x1b, %ax
    movw %ax, %ds
    movw %ax, %es
    pushq $0x1b
    pushq %rsi
    pushq $0x202
    pushq $0x23
    pushq %rdi
    movq %rdx, %rdi
    swapgs
    iretq
.size enterUserMode, . - enterUserMode

.global enterSystemService
.type enterSystemService, @function
// Legacy supervisor entry: install kernel data selectors and directly jump on
// the supplied supervisor stack. Unlike enterUserMode this does not construct
// a Ring3 frame or swap GS, because it stays privileged.
enterSystemService:
    cli
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movq %rdi, %rax
    movq %rsi, %rsp
    movq %rdx, %rdi
    xorq %rbp, %rbp
    sti
    jmp *%rax
.size enterSystemService, . - enterSystemService

.global resumeUserContext
.type resumeUserContext, @function
// RDI points to a saved SyscallContext. Copy its eighteen words onto this
// kernel stack with sixteen extra bytes reserved, matching entry.S's tail
// expansion. userContextReturn converts that tail to a five-word IRET frame
// and restores every GPR instead of losing RCX/R11 through SYSRET.
resumeUserContext:
    cli
    movq %rdi, %rsi
    subq $160, %rsp
    movq %rsp, %rdi
    movq $18, %rcx
    cld
    rep movsq
    jmp userContextReturn
.size resumeUserContext, . - resumeUserContext

.section .note.GNU-stack,"",@progbits
