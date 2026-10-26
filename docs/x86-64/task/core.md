# 用户态异常、信号与 ELF core

实现入口为 `src/kernel/cpu/interrupt/exceptions.cc`、`src/kernel/memory/fault.cc`、`src/kernel/task/signal.cc`、`src/kernel/task/core/core_dump.cc` 和 `src/task/core_dump.cc`。架构层采集异常，Pager 恢复合法缺页，信号层定向投递到故障线程，Task 统一终止进程，Ring3 task 服务负责文件写出。

## 向量策略

| 向量 | 策略 |
| --- | --- |
| 0 / #DE | SIGFPE/FPE_INTDIV，包括有符号除法商溢出 |
| 1 / #DB | 保存 DR6；BS 使用 TRAP_TRACE，硬件调试来源使用 TRAP_HWBKPT |
| 2 / NMI | IST2，系统诊断停机；不发送用户信号 |
| 3 / #BP | SIGTRAP/TRAP_BRKPT，保留 trap 后的 RIP；内核断点诊断停机 |
| 4 / #OF | SIGFPE/FPE_INTOVF；IDT 不开放用户 int 4，64 位 INTO 仍是 #UD |
| 5 / #BR | SIGSEGV/SEGV_BNDERR；未启用兼容模式/MPX 时不伪造触发测试 |
| 6 / #UD | SIGILL/ILL_ILLOPC；保留真实指令地址 |
| 7 / #NM | 非惰性 FPU 策略下属于配置故障，诊断停机 |
| 8 / #DF | IST1 应急诊断停机 |
| 9 / 旧协处理器段越界 | 废弃入口，诊断停机 |
| 10 / #TS | TSS/切栈机制故障，诊断停机 |
| 11 / #NP | 当前无 LDT/非 present 用户段，不能证明用户归责，按描述符/投递故障诊断停机 |
| 12 / #SS、13 / #GP | 用户栈错误或用户指令来源产生 SIGSEGV/SI_KERNEL，地址未知；非零 #SS 选择子错误、外部事件引发的 #GP 仍诊断停机 |
| 14 / #PF | 入口先读取 CR2；Pager 恢复成功后重试；未映射 MAPERR、权限 ACCERR、文件 EOF BUS_ADRERR、后端错误 BUS_OBJERR |
| 15 | 保留，诊断停机 |
| 16 / #MF | 未屏蔽 x87 状态分类为 FPE_FLT*；保存完整 FXSAVE 状态 |
| 17 / #AC | SIGBUS/BUS_ADRALN；不与 MOVAPS 的 #GP 混淆 |
| 18 / #MC | IST3，诊断停机 |
| 19 / #XM | 根据 MXCSR 状态和屏蔽位产生 FPE_FLT* |
| 20 / #VE、21 / #CP | 当前未开启相应特性，诊断停机，不伪造可恢复支持 |
| 22、23、24、25、26、27 | 各有 ISR，保留入口诊断停机 |
| 28 / #HV、29 / #VC、30 / #SX、31 | 未实现的平台协议/安全特性或保留入口，诊断停机 |

错误码向量为 8、10～14、17、21、29、30；其他 stub 补零。全部 256 个向量有入口。32～254 调用已注册 IRQ/IPI/本地 APIC 处理器并 EOI；未注册向量首次记录诊断并 EOI，I/O APIC 未分配引脚在初始化时屏蔽；255 为 spurious，不 EOI。不能用用户 `int n` 绕过 IDT DPL 来声称测试了硬件故障。

中断入口保存通用寄存器，并检查实际 GS base 处理 NMI 落在 swapgs 窗口的情况。内核自身 RIP 的异常始终进入内核诊断。旧引导服务使用受信任 SystemService 执行上下文时，只将低半区服务指令产生的可归责异常视作服务崩溃；内核函数内部错误仍停机。

## 故障与信号 ABI

`FaultRecord` 包含 PID/TID、启动纳秒标识、时间戳、向量/原始错误码、RIP/RSP、信号、代码、地址有效位、访问类型及 DR6。同步异常直接交给当前线程，不与进程 pending 位合并，普通异步信号不能覆盖其现场。

`os/signal_context.h` 固定本机 ABI，并与 libc 的 siginfo_t/ucontext_t 进行尺寸和偏移检查。SA_SIGINFO 传递真实 info 和 context；用户可修改寄存器及 FP 状态后恢复。普通异步信号仅提供当前已实现的来源信息；SIGCHLD 提供退出 PID、UID、CLD_EXITED/KILLED/DUMPED 和状态。

每线程保存替代栈、活动帧链和深度，最多嵌套 8 帧。每帧保留 128 字节 red zone，入口 RSP 满足 SysV 对齐。sigaltstack 最小 2048 字节，建议 SIGSTKSZ 或更大；当前正在替代栈内时禁止替换配置。栈配置只验证范围，实际可写性在投递时验证并按需 fault-in。

sigreturn 要求返回当前活动帧，校验 magic、用户 RIP/RSP、固定选择子和 FP 指针；MXCSR 只允许硬件报告的有效位。RFLAGS 清除 IOPL 等特权位，强制 IF 和保留位。非法返回强制 SIGSEGV。同步故障被忽略、屏蔽、处理器无效或栈写入失败时强制该信号默认动作；不无限重试构造失败的帧。

系统调用返回与 fork 上下文恢复使用 IRET，完整恢复通用寄存器。SignalReturn 是 syscall 20，restorer 直接执行该机制，不构造 IPC 请求。libc 直接向内核登记实际处理器，避免维护第二份处理器表和异步重入锁；pause/sigsuspend 使用每线程投递计数。系统调用和可抢占中断返回统一检查信号，因此没有系统调用的用户循环也可收到信号。断点、单步不统一调整 RIP。

浮点状态采用 512 字节 FXSAVE64，BSP/AP 设置 CR0.NE、MP，清除 EM/TS，设置 CR4.OSFXSR/OSXMMEXCPT。未启用 OSXSAVE/AVX。信号帧保存中断时 FP 状态，处理器在默认屏蔽环境运行，返回恢复用户修改后的 FP 状态。libc 提供标准 fenv 状态接口及 feenableexcept/fedisableexcept/fegetexcept。fegetenv 不改变当前屏蔽；feraiseexcept 通过 x87 等待异常真正触发已解屏蔽的 trap。

## 进程退出

致命默认动作调用统一 Task 退出，终止全部线程，并由现有回收线程取消 IPC/等待、停止设备、等待远端 CPU 离开地址空间，再释放资源。异常向量不再作为负退出码；兼容接口仍以负信号号表示信号退出，ExitInfo 保留独立的 signal/coreState 与故障记录。

waitpid 编码为信号号及实际 core 成功标志。仅 Complete/Truncated 设置 WCOREDUMP；Requested、Skipped、Failed、TimedOut 均不设置。父进程可以在所有 core 失败情况下回收子进程。`/proc/<pid>/status` 增加 ExitSignal、CoreState、FaultTid、FaultVector、FaultRip。监督器记录信号原因并执行既有重启策略。

## core 生命周期与权限

异常入口只记录请求。进程回收线程先终止并 quiesce 其他线程，在地址空间锁保护下复制有界快照；复制完即可回收原进程内存，不需要等待慢文件系统。快照由最多两个内核槽持有引用，写出 worker 只拿票据和有限读取接口，不能读任意进程或内核内存。

CoreConfigure/Acquire/Read/Finish 只允许固定 Task 角色。CoreRead 每次最多 4096 字节。票据包含 PID、启动标识、唯一序号、总长度和截止时间；写者身份必须匹配。独立内核超时线程在写出服务失联时释放槽和唤醒父进程。关键受监护服务不创建普通 core，避免 FS/task/MM 自身崩溃造成写盘依赖循环。

快照只包括驻留普通用户页及可信 Pager 管理的 RAM 页；不主动缺页、不读取 MMIO、framebuffer、DMA、命名共享区和其他外部映射。截断以独立状态与 OSINFO note 标识。最多 64 个线程、256 个程序头，单槽硬上限 16 MiB；默认两个 8 MiB 槽。共享/设备区排除属于策略，不是假装完整复制它们。

输出为 ELF64 ET_CORE/EM_X86_64，包含 PT_NOTE 和按连续地址、权限合并的 PT_LOAD。标准 note 使用 Linux x86-64 布局：NT_PRSTATUS（每线程）、NT_FPREGSET、NT_SIGINFO、NT_PRPSINFO、NT_AUXV 和 NT_FILE；OSINFO note 额外保存原始 FaultRecord。动态链接器为主程序及共享库登记文件段地址和页偏移，fork 继承、exec 清除，供 NT_FILE 使用。同时填写 DT_DEBUG，提供 SVR4 r_debug/link_map，使 GDB 能从快照中发现实际加载的共享库。静态程序调试应显式提供相同构建的可执行文件。

运行时唯一入口是 `/etc/core`，安装模板为 `script/etc/core`：

```ini
enabled=1
maximum_bytes=8388608
maximum_concurrent=2
maximum_space=67108864
timeout_ms=10000
directory=/var/core
```

配置在 task writer 启动时读取；格式或权限错误时停止启用转储并记录原因。maximum_bytes 与 RLIMIT_CORE 软限额取较小值；RLIMIT_CORE 随 fork/exec 保留，零限额禁止 core。凭据变化使当前地址空间不可转储，受保护服务同样跳过。

写出目录及所有祖先必须为 root 所有，祖先不可由组/其他用户写入，最终目录为私有目录。当前 VFS 不解析符号链接；目录必须是实际 Directory。文件用 O_EXCL、0600 创建 `.partial`，逐页写入并 fsync 后，以拒绝覆盖的硬链接发布最终 `core.PID.START.ID`，再删除 partial。目录内现有文件计入 maximum_space。文件系统不支持安全发布或写入失败时报告 Failed；不得输出 core dumped。文件系统永久阻塞时，内核仍按截止时间释放快照，磁盘中可能留下 `.partial`，其名称不会被当作成功 core。

## 验证入口与能力边界

永久回归位于 `tests/user-exceptions.cc`、`tests/user-exceptions.sh`、`tests/exception-emergency.sh`。先执行 `./configure && make -j build`，再执行 `sh tests/user-exceptions.sh`。隔离磁盘、core 和调试日志放在 `build/v0.1.3/`。

2026-10-23 在 WSL Debian、QEMU/KVM、4 vCPU 环境，正常矩阵 **60 项全部通过**，证据目录 `build/v0.1.3/run.PoLZb7/`。`serial.normal.log` 包含异常分类、替代栈、非法 sigreturn、完整 XMM 与 RCX/R11 恢复、x87/SSE 显式 trap、fork/exec、SIGCHLD、SIGCONT、忙循环信号投递，以及 shell 信号提示和受监护服务重启回收。故障应用与服务未引起无关 FS 服务重启。

实际生成单线程、多线程及 128 KiB 限额截断 core。`readelf.log`、`gdb.log`、`gdb-multithread.log` 验证 ELF notes、故障指令、调用栈、第二个 LWP，以及 libc.so、libc++.so、libos-c++.so 的已加载符号；`readelf-truncated.log` 验证截断产物仍有 PRSTATUS 和 OSINFO note，文件未超过限额。源码行号需要相同构建的调试信息，不能混用旧库或 SDK。

关闭转储、零剩余全局配额、100 ms 超时、只读目标卷、真实零空闲块目标卷五种失败策略均通过：故障进程仍能被 waitpid 回收，WTERMSIG 为 SIGILL，WCOREDUMP 不置位。证据是同目录的 `serial.disabled.log`、`serial.quota.log`、`serial.timeout.log`、`serial.readonly.log`、`serial.full.log`。只读和磁盘满使用独立 `/var/core` ext3 卷，根分区保持可写以正常启动账户及会话服务。用 `OSINFO_EXCEPTION_POLICY` 选择策略，`OSINFO_EXCEPTION_STAGE` 复用隔离目录；`smoke.normal.img` 保存正常矩阵镜像。

应急脚本通过 QEMU 注入真实 NMI，并关闭隔离 VM 内 #GP gate 的 present 位，使真实 #GP 触发 #DF。两种场景均进入对应异常向量并受控停机，证据目录 `build/v0.1.3/emergency.z68iIn/`。应急入口采用有界、无锁串口输出，panic 用 NMI 请求其他 CPU 停止，不进行 Task/FS 清理。

QEMU 10.0.6 TCG 在本环境对已解屏蔽 SSE 除零仅设置 MXCSR.ZE，未交付 #XM；浮点 trap 的通过证据来自 KVM。未启用 XSAVE/AVX；机器检查、CET、机密虚拟化及其他平台保留异常没有本轮硬件注入证据，不承诺恢复。
