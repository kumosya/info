/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/syscall/entry.S
 * SYSCALL stack entry and complete user-register restoration through IRET.
 */

#include "kernel/cpu.h"

.section .text
.code64
.global syscallEntry
.type syscallEntry, @function
.extern syscallDispatchEntry

// Offsets come from the CPU header shared with C++; hardcoding a second layout
// here could save a user RSP into an unrelated per-CPU field.
.set cpuKernelStackOffset, KERNEL_CPU_STACK_OFFSET
.set cpuSyscallUserStackOffset, KERNEL_CPU_USER_STACK_OFFSET

// SYSCALL supplies user RIP in RCX and flags in R11 but does not switch RSP.
// After swapgs, save user RSP in per-CPU storage before loading the current
// thread's kernel stack. IF is masked by FMASK during this vulnerable window.
syscallEntry:
    swapgs
    movq %rsp, %gs:cpuSyscallUserStackOffset
    movq %gs:cpuKernelStackOffset, %rsp
    // Reserve room for converting the three-word user continuation to the
    // five-word IRET tail later; it is not part of the eighteen-word C++
    // context.
    subq $16, %rsp

    // Push user RSP/RFLAGS/RIP, then RAX and the remaining GPRs in reverse
    // SyscallContext order. C++ receives a pointer to R15 at the lowest
    // address.
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
    // The complete frame and kernel stack are now established, so C++ may
    // allow interrupts while paging or waiting. Disable them again before
    // return-frame conversion and GS ownership changes.
    sti
    call syscallDispatchEntry
    cli

.global userContextReturn
// Shared return target for ordinary syscalls, fork resume and signal return.
// At offset 120 is RIP; replace the following three-word tail in place with
// RIP/CS/RFLAGS/RSP/SS, using the two words reserved on entry. Do this before
// restoring GPRs because RAX/RCX are temporary registers during conversion.
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
    // All kernel GS-relative work is complete. Switch to user GS immediately
    // before IRET; there must be no interruptible kernel continuation after
    // it.
    swapgs
    iretq
.size syscallEntry, . - syscallEntry

.section .note.GNU-stack,"",@progbits
