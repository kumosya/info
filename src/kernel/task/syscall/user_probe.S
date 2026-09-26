.section .rodata.user_probe,"a"
.code64
.global userSyscallProbeStart
.global userSyscallProbeEnd
userSyscallProbeStart:
    movq $5, %rax
    movq $0x900000, %rdi
    movq $4096, %rsi
    movq $1, %rdx
    syscall
    cmpq $0x900000, %rax
    jne 1f
    movabsq $0x6f73696e666f, %rbx
    movq %rbx, (%rax)

    movq $6, %rax
    movq $0x900000, %rdi
    movq $4096, %rsi
    syscall
    testq %rax, %rax
    jne 1f

    movq $3, %rax
    syscall
    movq $0, %rax
    xorq %rdi, %rdi
    syscall
1:
    movq $0, %rax
    movq $1, %rdi
    syscall
    ud2
userSyscallProbeEnd:

.section .note.GNU-stack,"",@progbits
