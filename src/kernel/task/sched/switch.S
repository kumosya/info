/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sched/switch.S
 * 内核栈切换，以及首次进入或恢复用户态执行。
 *
 * 文件职责
 * 提供四个从 C++ 调用的汇编入口：
 *   - switchThreadContext：保存当前内核现场，并换到另一个线程的内核栈；
 *   - enterUserMode：从内核态第一次进入 Ring3 用户态；
 *   - enterSystemService：进入特权级的系统服务入口，不构造用户态返回帧；
 *   - resumeUserContext：把保存的 SyscallContext 展开成 IRET 帧，恢复用户现场。
 *
 * 关键不变量
 * 1. 两边的内核栈布局必须严格互为镜像：压栈顺序与弹栈顺序相反，少压或多压
 *    一个寄存器都会让恢复出的 RIP 错位，进而跳到错误地址。
 * 2. switchThreadContext 发布旧 RSP 之后，旧栈上的局部变量就不能再访问；
 *    返回地址已经保存在旧栈里，只有该线程再次被调度时才会“返回”。
 * 3. 进入用户态必须在关中断的情况下构造好 IRET 帧；swapgs 之后到 iretq 之间
 *    不能再执行任何依赖内核 GS 的代码。
 *
 * 建议阅读顺序
 * 先看 switchThreadContext 理解一次上下文切换的寄存器保存/恢复，再看
 * enterUserMode 与 enterSystemService 的对比（是否构造 Ring3 帧、是否 swapgs），
 * 最后看 resumeUserContext 如何复用 entry.S 中的 userContextReturn。
 *
 * 术语速览
 * 上下文切换：保存当前执行现场（寄存器、栈指针、标志）并恢复另一个线程的现场。
 * RFLAGS：64 位标志寄存器，保存中断允许位 IF、方向位 DF 等，pushfq/popfq 成对使用。
 * IRET/IRETQ：中断返回指令，从栈上的帧恢复 RIP、CS、RFLAGS、RSP、SS，
 *   同时完成特权级与栈的切换；本例用它在 Ring0 与 Ring3 之间往返。
 * swapgs：交换 GS 基址寄存器。内核与用户态各用不同的 GS 基址（每 CPU 数据），
 *   进入和离开内核时必须成对切换，否则会读到错误的数据结构。
 * Ring3/Ring0：x86 特权级，3 为用户态，0 为内核态。
 */

.section .text
.code64
.global switchThreadContext
.type switchThreadContext, @function

/*
 * switchThreadContext —— 切换内核执行栈，是上下文切换最底层的一步。
 * 输入：RDI = 旧线程保存 RSP 的槽位地址（uint64_t*），RSI = 新线程已保存的 RSP。
 * 做什么：先把 RFLAGS 和全部 15 个通用寄存器压到旧线程的内核栈上，再把当前 RSP
 * 写回 *RDI，然后换成 RSI 指向的新栈，按相反顺序弹出同样的寄存器。
 * 为什么是这个顺序：进入函数时返回地址已经在栈的最高处，pushfq 把 RFLAGS 放在
 * 它下面，15 个寄存器再依次压到更低地址；弹出顺序相反，最后 popfq 和 ret
 * 才能按“15 个寄存器 + 标志 + 返回地址”的正确次序还原。
 * 关键点：寄存器全部压完之后才允许改写 RDI/RSI；执行 movq %rsi, %rsp 之后，
 * 弹出的就是另一个线程的帧，旧栈上的任何局部引用都不能再用，
 * 直到本线程再次被调度。
 * 与新线程的配合：新线程的栈由 Thread::buildInitialStack() 预先摆成同样的布局，
 * 只是返回地址换成 Thread::bootstrap，因此第一次切换会“返回”到 bootstrap。
 * 前提：调用者已关中断（由 schedule() 保证），并且两个线程的内核栈都有效。
 */
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
    // 寄存器已经保存好，现在可以用 RDI/RSI 选两个栈了。
    // 完成下面的 RSP 赋值后，弹栈读到的就是另一个线程的帧；
    // 在它再次被调度之前，不能再使用任何指向旧栈的局部引用。

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

/*
 * enterUserMode —— 第一次从内核态进入用户态（Ring3）。
 * 输入：RDI = 用户态入口地址，RSI = 用户栈指针，RDX = 传给入口的初始参数。
 * 做什么：关中断，把 DS/ES 换成用户数据段选择子 0x1b，然后按 IRETQ 要求的顺序
 * 压入 SS=0x1b、RSP、RFLAGS=0x202（IF=1，即开中断）、CS=0x23、RIP；
 * 最后把参数搬到 RDI，执行 swapgs 和 iretq。
 * 为什么用 IRET 而不用 SYSRET：IRET 能一次完成特权级切换、栈切换和标志恢复，
 * 并且从帧里的 RFLAGS 打开中断，返回用户态后就能响应时钟中断。
 * 为什么 swapgs 放在最后：它会切换 GS 基址，之后内核 GS 相对的数据就不能再访问，
 * 所以必须等所有内核侧准备工作结束，紧挨着 iretq 执行。
 * 前提：调用者（Thread::bootstrap）已经切到新线程的内核栈上；本函数自己 cli，
 * 防止构造帧的过程中被中断打断。
 * 返回：不返回——IRETQ 之后就在用户态执行。
 */
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

.global enterSystemService
.type enterSystemService, @function

/*
 * enterSystemService —— 进入特权级系统服务（Sys 线程）的旧式入口。
 * 输入：RDI = 服务入口地址，RSI = 要使用的特权级栈，RDX = 传给入口的参数。
 * 做什么：关中断，装载内核数据段选择子 0x10，把 RSI 设为新的 RSP，参数搬到 RDI，
 * 清空 RBP（帧指针，表示没有外层调用者），开中断后跳到服务入口。
 * 与 enterUserMode 的区别：这里始终停留在 Ring0，不构造 Ring3 的 IRET 帧，
 * 也不做 swapgs；它只是换栈并跳转，因此服务代码运行在特权态。
 * 前提：调用者已处于内核态；跳转后不会自动返回，服务入口负责后续流程。
 */
enterSystemService:
    cli
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movq %rdi, %rax
    movq %rsi, %rsp
    movq %rdx, %rdi
    xorq %rbp, %rbp
    sti
    jmp *%rax
.size enterSystemService, . - enterSystemService

.global resumeUserContext
.type resumeUserContext, @function

/*
 * resumeUserContext —— 恢复保存的用户态现场并返回用户态。
 * 输入：RDI 指向一个已保存的 SyscallContext（由系统调用入口或 fork 保存）。
 * 做什么：关中断，把 18 个字（每字 8 字节）从该结构复制到当前内核栈，
 * 并额外预留 16 字节，然后跳到 entry.S 的 userContextReturn。
 * 为什么预留 16 字节：userContextReturn 要在原地把“3 字尾部”扩展成 5 字 IRET 帧，
 * 需要两个字的空位，布局必须与 entry.S 完全一致。
 * 为什么不用 SYSRET：SYSRET 无法恢复 RCX/R11，也不能恢复信号返回需要的所有标志位；
 * 统一走 IRET 路径可以恢复全部通用寄存器。
 * 前提：在当前线程的内核栈上执行；复制过程不能被中断打断（cli 保证），
 * cld 保证 rep movsq 按地址递增方向复制。
 * 返回：不返回——控制权交给 userContextReturn，最终 iretq 回到用户态。
 */
resumeUserContext:
    cli
    movq %rdi, %rsi
    subq $160, %rsp
    movq %rsp, %rdi
    movq $18, %rcx
    cld
    rep movsq
    jmp userContextReturn
.size resumeUserContext, . - resumeUserContext

.section .note.GNU-stack,"",@progbits
