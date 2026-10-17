/* Compiler-required memory primitives; no user library is linked into the kernel. */
.section .text
.code64
.global memset
.type memset, @function
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
memcpy:
    movq %rdi, %rax
    movq %rdx, %rcx
    rep movsb
    ret
.size memcpy, . - memcpy
.global memmove
.type memmove, @function
memmove:
    movq %rdi, %rax
    movq %rdx, %rcx
    cmpq %rsi, %rdi
    jbe 1f
    leaq -1(%rdi,%rdx), %rdi
    leaq -1(%rsi,%rdx), %rsi
    std
    rep movsb
    cld
    ret
1:  rep movsb
    ret
.size memmove, . - memmove
.section .note.GNU-stack,"",@progbits
