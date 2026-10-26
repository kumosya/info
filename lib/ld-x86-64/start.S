.section .text.start
.global _start
.type _start,@function
_start:
    xorq %rbp, %rbp
    movq %rsp, %rdi
    andq $-16, %rsp
    call osinfoLoaderStart
    ud2
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
