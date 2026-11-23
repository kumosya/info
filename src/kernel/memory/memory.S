/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/memory.S
 * 内核编译所需的独立内存操作。
 */

// 内核不链接用户运行库，这些基础内存函数供编译器生成的代码调用。
.section .text
.code64
.global memset
.type memset, @function

/*
 * 按调用约定，RDI 是目标、ESI 是字节值、RDX 是长度；先保存原目标，REP 修改指针后仍返回原地址。调用时方向标志应已清零。
 */
memset:
    movq %rdi, %r8
    movq %rdx, %rcx
    movl %esi, %eax
    rep stosb
    movq %r8, %rax
    ret
.size memset, . - memset

.global memcpy
.type memcpy, @function

/*
 * 从 RSI 向 RDI 正向复制 RDX 字节，返回原目标；重叠区域应使用 memmove。
 */
memcpy:
    movq %rdi, %rax
    movq %rdx, %rcx
    rep movsb
    ret
.size memcpy, . - memcpy

.global memmove
.type memmove, @function

/*
 * 先判断复制方向；目标地址较高时倒序复制，防止覆盖仍未读取的源字节。
 */
memmove:
    movq %rdi, %rax
    movq %rdx, %rcx
    cmpq %rsi, %rdi
    jbe 1f
    leaq -1(%rdi,%rdx), %rdi
    leaq -1(%rsi,%rdx), %rsi

    // 倒序只用于这次复制，返回前必须清除方向标志，否则后续编译器生成的字符串操作也会反向执行。
    std
    rep movsb
    cld
    ret
1:  rep movsb
    ret
.size memmove, . - memmove

.section .note.GNU-stack,"",@progbits
