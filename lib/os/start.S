.section .text.start
.code64
.global _start
.type _start, @function
.extern main
.extern osRuntimeInit
.extern exit
.extern __libc_set_environ
.extern __libc_import_exec_state

_start:
    xorq %rbp, %rbp
    movq %rsp, %r12
    movq (%r12), %rdi
    leaq 8(%r12), %rsi
    leaq 16(%r12,%rdi,8), %rdx
    movq %rdx, %r14
    call osRuntimeInit
    movq %rax, %r13
    movq %r14, %rdi
    call __libc_set_environ
    movq %r14, %rdi
1:
    cmpq $0, (%rdi)
    je 2f
    addq $8, %rdi
    jmp 1b
2:
    movq 8(%rdi), %rdi
    testq %rdi, %rdi
    jz 3f
    call __libc_import_exec_state
3:
    movq %r12, %rsp
    movq %r13, %rdi
    leaq 8(%rsp), %rsi
    movq %r14, %rdx
    andq $-16, %rsp
    call main
    movl %eax, %edi
    call exit
4:
    ud2
    jmp 4b
.size _start, . - _start

.section .note.GNU-stack,"",@progbits
