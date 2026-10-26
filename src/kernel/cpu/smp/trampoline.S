.section .ap_trampoline,"ax"
.code16
.global apTrampolineStart
.global apTrampolineEnd
.global apStartupCr3
.global apStartupStack
.global apStartupCpuLocal
.global apStartupEntry
.set apPhysicalBase, 0x8000

apTrampolineStart:
    cli
    cld
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    lgdtl apPhysicalBase + (apGdtPointer - apTrampolineStart)
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $apPhysicalBase + (apProtectedEntry - apTrampolineStart)

.code32
apProtectedEntry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl %cr4, %eax
    orl $((1 << 5) | (1 << 9) | (1 << 10)), %eax
    movl %eax, %cr4
    movl apPhysicalBase + (apStartupCr3 - apTrampolineStart), %eax
    movl %eax, %cr3
    movl $0xc0000080, %ecx
    rdmsr
    orl $((1 << 8) | (1 << 11)), %eax
    wrmsr
    movl %cr0, %eax
    andl $~((1 << 2) | (1 << 3)), %eax
    orl $((1 << 31) | (1 << 18) | (1 << 5) | (1 << 1) | 1), %eax
    movl %eax, %cr0
    ljmpl $0x18, $apPhysicalBase + (apLongModeEntry - apTrampolineStart)

.code64
apLongModeEntry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movq apStartupStack(%rip), %rsp
    movq apStartupCpuLocal(%rip), %rdi
    movq %rdi, %rax
    movl $0xc0000101, %ecx
    movq %rax, %rdx
    shrq $32, %rdx
    wrmsr
    movq apStartupEntry(%rip), %rax
    call *%rax
1:
    cli
    hlt
    jmp 1b

.align 8
apGdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00af9a000000ffff
apGdtPointer:
    .word apGdtPointer - apGdt - 1
    .long apPhysicalBase + (apGdt - apTrampolineStart)

.align 8
apStartupCr3:
    .quad 0
apStartupStack:
    .quad 0
apStartupCpuLocal:
    .quad 0
apStartupEntry:
    .quad 0
apTrampolineEnd:

.section .note.GNU-stack,"",@progbits
