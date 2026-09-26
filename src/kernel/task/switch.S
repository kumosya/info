.section .text
.code64
.global switchThreadContext
.type switchThreadContext, @function
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

.global resumeUserContext
.type resumeUserContext, @function
resumeUserContext:
    cli
    movq %rdi, %rsp
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
    popq %rcx
    popq %r11
    movq (%rsp), %rsp
    swapgs
    sysretq
.size resumeUserContext, . - resumeUserContext

.section .note.GNU-stack,"",@progbits
