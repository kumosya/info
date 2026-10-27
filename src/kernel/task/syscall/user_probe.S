/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/syscall/user_probe.S
 * User-mode bootstrap probe for the native system call boundary.
 */

#include <os/syscall.h>
#include "service/common/entry.h"
// The boot path copies the bytes between the exported labels into a user page.
// Keep the code position-independent and avoid kernel-only addresses; RDI
// initially contains a task-kernel endpoint handle, not a function pointer.
.section .rodata.user_probe,"a"
.code64
.global userSyscallProbeStart
.global userSyscallProbeEnd
// Reserve two 4224-byte IpcMessage buffers: R13 is request, R14 is reply. Keep
// the endpoint in callee-preserved R12 across native syscalls.
userSyscallProbeStart:
    movq %rdi, %r12
    subq $8448, %rsp
    movq %rsp, %r13
    leaq 4224(%rsp), %r14
    movq %rsp, %rdi
    movq $1056, %rcx
    xorq %rax, %rax
    rep stosq
    // After zeroing both messages, request Yield through CallService rather
    // than a retired dedicated yield syscall. length is at byte offset 32; R10
    // = -1 encodes the infinite IPC deadline.
    movq $OS_TASK_YIELD_OPERATION, (%r13)
    movl $5, 32(%r13)
    movq $OS_SYS_CALL_SERVICE, %rax
    movq %r12, %rdi
    movq %r13, %rsi
    movq %r14, %rdx
    movq $-1, %r10
    syscall
    // Reuse the same call envelope for process exit. A successful exit must
    // not return; UD2 makes an unexpected return an observable user fault.
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
