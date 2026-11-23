/* 引导 CPU 入口：验证能力、建立临时页表并切换到 64 位内核。 */
.section .multiboot,"a"
.align 8

/* Multiboot2 头给出魔数、长度与校验和，后面的标签请求显存信息及建议显示模式。 */
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

/* 引导器以 32 位模式跳到这里；EBX 保存信息地址，EAX 必须带约定的引导魔数。 */
_start:
    cli
    movl %ebx, multibootAddress
    cmpl $0x36d76289, %eax
    jne bootFailure
    movl $bootStackTop, %esp
    call checkLongMode

    /* 在启用分页前准备低地址恒等映射和高地址直接映射，让切换前后的指令与栈都可访问。 */
    call setupPageTables

    /* 装入临时 GDT，配置浮点、SSE 和 PAE，再让 CR3 指向新建页表。 */
    lgdt gdtPointer
    movl %cr0, %eax
    andl $~((1 << 2) | (1 << 3)), %eax
    orl $((1 << 1) | (1 << 5) | (1 << 18)), %eax
    movl %eax, %cr0
    movl %cr4, %eax
    orl $((1 << 5) | (1 << 9) | (1 << 10)), %eax
    movl %eax, %cr4
    movl $pageMapPml4, %eax
    movl %eax, %cr3

    /* EFER 同时开启长模式允许位与不可执行页支持；真正切换还需要打开 CR0 分页位。 */
    movl $0xc0000080, %ecx
    rdmsr
    orl $((1 << 8) | (1 << 11)), %eax
    wrmsr

    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0

    /* 远跳转刷新代码段，选择 64 位代码描述符后，后续指令才能按 64 位方式解释。 */
    ljmp $0x10, $start64

/* 此时高级日志尚不可用，直接向 COM1 输出 B 作为启动失败标记，然后关中断停机。 */
bootFailure:
    movw $0x3f8, %dx
    movb $0x42, %al     /* 字符 B */
    outb %al, %dx
1:
    cli
    hlt
    jmp 1b

/* 清空整组启动页表，用 2 MiB 大页同时覆盖前 64 GiB 的低地址和内核直接映射地址。 */
setupPageTables:
    movl $pageMapPml4, %edi
    xorl %eax, %eax
    movl $(4096 * 66 / 4), %ecx
    rep stosl

    /* PML4 的零号项服务低地址，256 号项对应 0xffff800000000000，二者共用同一棵下级页表。 */
    movl $pageDirectoryPointerTable, %eax
    orl $0x3, %eax
    movl %eax, pageMapPml4
    movl $0, pageMapPml4 + 4
    movl %eax, pageMapPml4 + (256 * 8)
    movl $0, pageMapPml4 + (256 * 8) + 4

    movl $pageDirectories, %edi
    xorl %ecx, %ecx

    /* 为 64 个页目录分别填写指针，每个目录负责 1 GiB 的物理地址范围。 */
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

    /* 每项映射 2 MiB；物理地址超过 32 位的部分放在页表项高半部，不能只保留 EAX。 */
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

/* 已进入长模式：清方向标志、初始化浮点，建立数据段和高地址启动栈，再按 C++ 调用约定传参。 */
start64:
    cld
    fninit
    movw $0x18, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* 把启动栈地址加上直接映射基址，C++ 内核之后可撤销不再需要的低地址映射。 */
    movabs $bootStackTop, %rsp
    movabs $0xffff800000000000, %rax
    addq %rax, %rsp

    /* 四个参数依次为引导信息物理地址、页表根、启动保留区起点和内核装载末尾。 */
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

/* 翻转 EFLAGS 的 ID 位测试 CPUID 是否存在，再检查 SSE、SSE2、长模式和 NX 支持。 */
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

    /* 先查扩展页上限，不能直接假定长模式能力页可读。 */
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

/* 临时段表：空槽、32 位代码、64 位代码和数据段；正式的每 CPU 段表由 C++ 初始化。 */
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

/* 静态预留启动页表，不能在物理页分配器初始化之前依赖动态内存。 */
pageMapPml4:
    .skip 4096
pageDirectoryPointerTable:
    .skip 4096
.global pageTablesEnd
pageDirectories:
    .skip 4096 * 64
pageTablesEnd:
.align 16

/* 启动栈预留 16 KiB，栈顶标签指向保留空间末尾，栈从这里向低地址增长。 */
bootStack:
    .skip 16384
bootStackTop:
