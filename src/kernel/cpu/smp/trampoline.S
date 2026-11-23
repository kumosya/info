/* 新 CPU 的低地址跳板：实模式进入保护模式，再使用引导 CPU 准备的页表进入长模式。 */
.section .ap_trampoline,"ax"
.code16
.global apTrampolineStart
.global apTrampolineEnd
.global apStartupCr3
.global apStartupStack
.global apStartupCpuLocal
.global apStartupEntry
.set apPhysicalBase, 0x8000

/* 整段会复制到物理 0x8000；实模式下段基址清零，所有绝对位置使用副本地址加符号相对偏移。 */
apTrampolineStart:
    cli
    cld
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp

    /* 临时 GDT 也位于副本中；开启保护模式后，用远跳转切到 32 位代码段。 */
    lgdtl apPhysicalBase + (apGdtPointer - apTrampolineStart)
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $apPhysicalBase + (apProtectedEntry - apTrampolineStart)

.code32

/* 先装数据段，再打开 PAE 与 SSE 支持，CR3 使用引导 CPU 写入的内核页表根。 */
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

    /* EFER 打开长模式和 NX，CR0 再打开分页；远跳转选择 64 位代码段完成模式交接。 */
    movl $0xc0000080, %ecx
    rdmsr
    orl $((1 << 8) | (1 << 11)), %eax
    wrmsr
    /* 进入 C++ 后由本 CPU 的初始化过程统一启用缓存。 */
    movl %cr0, %eax
    andl $~((1 << 2) | (1 << 3)), %eax
    orl $((1 << 31) | (1 << 18) | (1 << 5) | (1 << 1) | 1), %eax
    movl %eax, %cr0
    ljmpl $0x18, $apPhysicalBase + (apLongModeEntry - apTrampolineStart)

.code64

/* 已能访问内核高地址，读取本 CPU 的专用栈、局部状态与 C++ 入口，不能继续共用临时低地址栈。 */
apLongModeEntry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movq apStartupStack(%rip), %rsp
    movq apStartupCpuLocal(%rip), %rdi

    /* 提前把 GS 基址设为本 CPU 对象，C++ 入口即可通过 GS 读取当前 CPU 信息。 */
    movq %rdi, %rax
    movl $0xc0000101, %ecx
    movq %rax, %rdx
    shrq $32, %rdx
    wrmsr

    /* RDI 已是第一个 C++ 参数；入口不应返回，若意外返回则保持关中断并停机。 */
    movq apStartupEntry(%rip), %rax
    call *%rax
1:
    cli
    hlt
    jmp 1b

.align 8

/* 跳板专用段表，含 32 位代码、数据和 64 位代码；正式描述符随后由新 CPU 自己加载。 */
apGdt:
    .quad 0
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00af9a000000ffff
apGdtPointer:
    .word apGdtPointer - apGdt - 1
    .long apPhysicalBase + (apGdt - apTrampolineStart)

.align 8

/* 以下四个八字节参数由引导 CPU 逐个启动时改写，必须等前一个 CPU 接手后才能供下一个使用。 */
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
