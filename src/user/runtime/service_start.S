.section .text.start
.code64
.global _start
.type _start, @function
.extern userMain
.extern _ZN2os11runtimeInitEiPPcS1_
.extern _ZN2os11runtimeFiniEv

_start:
    xorq %rbp, %rbp
    movq %rsp, %r12
    movq (%r12), %rdi
    leaq 8(%r12), %rsi
    leaq 16(%r12,%rdi,8), %rdx
    call _ZN2os11runtimeInitEiPPcS1_
    movq %r12, %rsp
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    leaq 16(%rsp,%rdi,8), %rdx
    andq $-16, %rsp
    call userMain
    movq %rax, %r12
    call _ZN2os11runtimeFiniEv
    movq %r12, %rdi
    xorq %rax, %rax
    syscall
1:
    ud2
    jmp 1b
.size _start, . - _start

.section .note.GNU-stack,"",@progbits
