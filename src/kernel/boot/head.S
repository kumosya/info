.section .multiboot,"a"
.align 8
multibootHeader:
    .long 0xe85250d6
    .long 0
    .long multibootHeaderEnd - multibootHeader
    .long -(0xe85250d6 + (multibootHeaderEnd - multibootHeader))
    .word 1
    .word 0
    .long 12
    .long 8
    .align 8
    .word 5
    .word 1
    .long 20
    .long 1024
    .long 768
    .long 32
    .align 8
    .word 0
    .word 0
    .long 8
multibootHeaderEnd:

.section .boot.text,"ax"
.code32
.global _start
.extern kernelEntry
.extern __bootStart
.extern __kernelLoadEnd

_start:
    cli
    movl %ebx, multibootAddress
    cmpl $0x36d76289, %eax
    jne bootFailure
    movl $bootStackTop, %esp
    call checkLongMode

    call setupPageTables

    lgdt gdtPointer
    movl %cr0, %eax
    andl $~(1 << 2), %eax
    orl $(1 << 1), %eax
    movl %eax, %cr0
    movl %cr4, %eax
    orl $((1 << 5) | (1 << 9) | (1 << 10)), %eax
    movl %eax, %cr4
    movl $pageMapPml4, %eax
    movl %eax, %cr3

    movl $0xc0000080, %ecx
    rdmsr
    orl $((1 << 8) | (1 << 11)), %eax
    wrmsr

    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0
    ljmp $0x10, $start64

bootFailure:
    movw $0x3f8, %dx
    movb $0x42, %al
    outb %al, %dx
1:
    cli
    hlt
    jmp 1b

setupPageTables:
    movl $pageMapPml4, %edi
    xorl %eax, %eax
    movl $(4096 * 66 / 4), %ecx
    rep stosl

    movl $pageDirectoryPointerTable, %eax
    orl $0x3, %eax
    movl %eax, pageMapPml4
    movl $0, pageMapPml4 + 4
    movl %eax, pageMapPml4 + (256 * 8)
    movl $0, pageMapPml4 + (256 * 8) + 4

    movl $pageDirectories, %edi
    xorl %ecx, %ecx
2:
    movl %edi, %eax
    orl $0x3, %eax
    movl %eax, pageDirectoryPointerTable(,%ecx,8)
    movl $0, pageDirectoryPointerTable + 4(,%ecx,8)
    addl $4096, %edi
    incl %ecx
    cmpl $64, %ecx
    jne 2b

    movl $pageDirectories, %edi
    xorl %ecx, %ecx
3:
    movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax
    movl %ecx, %edx
    shrl $11, %edx
    movl %eax, (%edi)
    movl %edx, 4(%edi)
    addl $8, %edi
    incl %ecx
    cmpl $(64 * 512), %ecx
    jne 3b
    ret

.code64
start64:
    cld
    fninit
    movw $0x18, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movabs $bootStackTop, %rsp
    movabs $0xffff800000000000, %rax
    addq %rax, %rsp
    movl multibootAddress, %edi
    movq %cr3, %rsi
    movabs $__bootStart, %rdx
    movabs $__kernelLoadEnd, %rcx
    movabs $kernelEntry, %rax
    call *%rax
4:
    cli
    hlt
    jmp 4b

.code32
checkLongMode:
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x200000, %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    xorl %ecx, %eax
    jz bootFailure
    movl $1, %eax
    cpuid
    movl %edx, %eax
    andl $((1 << 25) | (1 << 26)), %eax
    cmpl $((1 << 25) | (1 << 26)), %eax
    jne bootFailure
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb bootFailure
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx
    jz bootFailure
    testl $(1 << 20), %edx
    jz bootFailure
    ret

.code64
.section .boot.rodata,"a"
.align 8
gdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdtPointer:
    .word . - gdt - 1
    .long gdt

.section .boot.data,"aw"
.align 4
multibootAddress:
    .long 0

.section .boot.bss,"aw"
.align 4096
.global pageMapPml4
.global bootStack
.global bootStackTop
pageMapPml4:
    .skip 4096
pageDirectoryPointerTable:
    .skip 4096
.global pageTablesEnd
pageDirectories:
    .skip 4096 * 64
pageTablesEnd:
.align 16
bootStack:
    .skip 16384
bootStackTop:
