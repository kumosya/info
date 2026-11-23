/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/syscall/user_probe.S
 * 启动时检查用户态系统调用边界。
 */

#include <os/syscall.h>
#include "service/common/entry.h"

// 启动流程把两个导出标签之间的机器码复制到用户页；代码必须与装载位置无关。RDI 最初保存任务内核端点句柄。
.section .rodata.user_probe,"a"
.code64
.global userSyscallProbeStart
.global userSyscallProbeEnd

/*
 * 预留两份 4224 字节消息，R13 指向请求、R14 指向回复；R12 跨系统调用保存端点句柄。
 */
userSyscallProbeStart:
    movq %rdi, %r12
    subq $8448, %rsp
    movq %rsp, %r13
    leaq 4224(%rsp), %r14
    movq %rsp, %rdi
    movq $1056, %rcx
    xorq %rax, %rax
    rep stosq

    // 两份消息清零后，通过 CallService 请求让出 CPU；消息长度在偏移 32，R10 为 -1 表示无限等待。
    movq $OS_TASK_YIELD_OPERATION, (%r13)
    movl $5, 32(%r13)
    movq $OS_SYS_CALL_SERVICE, %rax
    movq %r12, %rdi
    movq %r13, %rsi
    movq %r14, %rdx
    movq $-1, %r10
    syscall

    // 复用消息请求进程退出；成功退出不应返回，若意外返回则用 UD2 触发可观察的用户异常。
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
