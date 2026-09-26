.section .text
.code64
.extern interruptDispatch

.macro isrNoError vector
.global isr\vector
isr\vector:
    pushq $0
    pushq $\vector
    jmp isrCommon
.endm

.macro isrError vector
.global isr\vector
isr\vector:
    pushq $\vector
    jmp isrCommon
.endm

isrNoError 0
isrNoError 1
isrNoError 2
isrNoError 3
isrNoError 4
isrNoError 5
isrNoError 6
isrNoError 7
isrError 8
isrNoError 9
isrError 10
isrError 11
isrError 12
isrError 13
isrError 14
isrNoError 15
isrNoError 16
isrError 17
isrNoError 18
isrNoError 19
isrNoError 20
isrError 21
isrNoError 22
isrNoError 23
isrNoError 24
isrNoError 25
isrNoError 26
isrNoError 27
isrNoError 28
isrError 29
isrError 30
isrNoError 31

.altmacro
.macro createRemainingIsr vector
isrNoError %vector
.endm
.set currentVector, 32
.rept 224
createRemainingIsr %currentVector
.set currentVector, currentVector + 1
.endr
.noaltmacro

isrCommon:
    cld
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

    testb $3, 144(%rsp)
    jz 1f
    swapgs
1:
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call interruptDispatch
    movq %rbx, %rsp

    testb $3, 144(%rsp)
    jz 2f
    swapgs
2:
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
    addq $16, %rsp
    iretq

.section .data.rel.ro,"aw",@progbits
.global isrStubTable
isrStubTable:
.altmacro
.macro createTableEntry vector
    .quad isr\vector
.endm
.set currentVector, 0
.rept 256
createTableEntry %currentVector
.set currentVector, currentVector + 1
.endr
.noaltmacro

.section .note.GNU-stack,"",@progbits
