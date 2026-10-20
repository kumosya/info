.section .text.start
.code64
.global _start
.type _start, @function
.extern main
.extern osRuntimeInit
.extern osRuntimeFini

_start:
    # Service images must never execute with kernel privilege.
    movw %cs, %ax
    andw $3, %ax
    cmpw $3, %ax
    jne 1f
    xorq %rbp, %rbp
    movq %rsp, %r12
    movq (%r12), %rdi
    leaq 8(%r12), %rsi
    leaq 16(%r12,%rdi,8), %rdx
    call osRuntimeInit
    movq %rax, %r13
    movq %r12, %rsp
    movq %r13, %rdi
    leaq 8(%rsp), %rsi
    andq $-16, %rsp
    call main
    movq %rax, %r12
    call osRuntimeFini
    movq %r12, %rdi
    call _ZN2os8exitTaskEi
1:
    ud2
    jmp 1b
.size _start, . - _start

.section .note.GNU-stack,"",@progbits
