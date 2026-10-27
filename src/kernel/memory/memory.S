/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/memory.S
 * Freestanding byte-memory primitives required by the kernel compiler.
 */

// Compiler-required memory primitives; no user library is linked into the
// kernel.
.section .text
.code64
.global memset
.type memset, @function
// SysV RDI = destination, ESI = byte value, RDX = byte count. Save the
// original destination for RAX while REP advances RDI. Kernel callers must
// enter with the direction flag clear, as required by the C ABI.
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
// Copy forward from RSI to RDI for RDX bytes; overlap is the caller's error.
// RAX keeps the original destination even after REP changes both pointers.
memcpy:
    movq %rdi, %rax
    movq %rdx, %rcx
    rep movsb
    ret
.size memcpy, . - memcpy
.global memmove
.type memmove, @function
// Choose direction before modifying pointers. Destination at/below source can
// copy forward; a higher destination copies backward so writes do not destroy
// source bytes still needed by an overlapping region.
memmove:
    movq %rdi, %rax
    movq %rdx, %rcx
    cmpq %rsi, %rdi
    jbe 1f
    leaq -1(%rdi,%rdx), %rdi
    leaq -1(%rsi,%rdx), %rsi
    // Set backward direction only for the copy, then restore DF before return.
    // Leaving it set would reverse unrelated compiler-generated memory
    // operations.
    std
    rep movsb
    cld
    ret
1:  rep movsb
    ret
.size memmove, . - memmove
.section .note.GNU-stack,"",@progbits
