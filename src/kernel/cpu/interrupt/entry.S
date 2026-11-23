/* 统一中断现场布局，保存寄存器并调用 C++ 分发，结束后用 IRETQ 恢复被打断的执行。 */
.section .text
.code64
.extern interruptDispatch

/* 不带硬件错误码的向量补一个零，使公共入口始终看到“向量、错误码、硬件现场”的相同布局。 */
.macro isrNoError vector
.global isr\vector
isr\vector:
    pushq $0
    pushq $\vector
    jmp isrCommon
.endm

/* 这类异常由 CPU 已经压入错误码，只补向量号，不能再多压一个零。 */
.macro isrError vector
.global isr\vector
isr\vector:
    pushq $\vector
    jmp isrCommon
.endm

/* 前 32 项是 CPU 异常，按架构区分哪些入口已经自带错误码。 */
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

/* 其余 224 个入口统一按无错误码生成，供设备、中断定时器和跨 CPU 通知使用。 */
.macro createRemainingIsr vector
isrNoError %vector
.endm
.set currentVector, 32
.rept 224
createRemainingIsr %currentVector
.set currentVector, currentVector + 1
.endr
.noaltmacro

/* 压栈次序必须与 C++ Context 字段一致；清方向标志以满足编译器生成的字符串操作约定。 */
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

    /* NMI 可能打断内核尚未完成 SWAPGS 的窗口，因此检查实时 GS，而不能仅看保存的 CS。 */

    /* GS 高半部为负表示当前已指向内核局部状态；需要切换时记下标志，返回前仅撤销本次切换。 */
    xorl %r12d, %r12d
    movl $0xc0000101, %ecx
    rdmsr
    testl %edx, %edx
    js 1f
    swapgs
    movl $1, %r12d
1:

    /* RDI 传原始现场地址，RBX 保留未对齐栈顶；调用 C++ 前把 RSP 向下对齐到十六字节。 */
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call interruptDispatch
    movq %rbx, %rsp

    /* C++ 返回后恢复原栈位置，并按进入时的记录还原 GS，不能只凭目标 CS 决定是否交换。 */
    testl %r12d, %r12d
    jz 2f
    swapgs
2:

    /* 逆序恢复通用寄存器，丢弃统一压入的向量与错误码，再由硬件恢复指令地址、标志和栈。 */
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

/* 生成 256 个入口指针，C++ 建立 IDT 时按向量号直接索引，避免手工维护两份对应表。 */
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
