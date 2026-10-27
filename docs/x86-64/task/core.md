# 信号、用户异常与 ELF core

进程信号和用户信号现场由 `src/kernel/task/signal.cc` 实现，core 快照及其生命期由 `core/core_dump.cc` 实现；声明位于 `include/kernel/task.h`，命名空间为 `kernel::task`。CPU 异常分类在 `src/kernel/cpu/interrupt/exceptions.cc`，缺页处理在 `src/kernel/memory/fault.cc`。文件写出由用户态 `src/task/core_dump.cc` 完成。

## 信号状态

Task 保存 SignalAction 表和进程 pending 位图；Thread 保存线程 pending、屏蔽字、替代栈、活动帧地址、帧深度、投递计数，以及最近用户现场和故障记录。

SignalAction 保存 handler、mask、flags、restorer。handler 为 0 表示默认、1 表示忽略，大于 1 表示用户处理函数。支持的标志包含 NoDefer、ResetHand、OnStack 以及服务入口接受的 SIGINFO/RESTART 标志；接受 RESTART 标志本身不表示所有被中断操作都有自动重启实现。

Signal 枚举采用 POSIX 风格编号：Kill=9、Child=17、Continue=18、Stop=19、WindowChanged=28，最高可表示信号号为 64。Kill 和 Stop 不允许捕获或屏蔽；位图索引为信号号减一。普通 pending 位图会合并同号信号，不提供实时信号逐项排队。

| 函数 | 功能与参数 |
| --- | --- |
| `Task::setSignalAction(signal, action, oldAction)` | 安装动作，oldAction 非空时返回旧动作；拒绝 Kill/Stop。 |
| `getSignalAction(signal, action)` | 读取指定动作。 |
| `pendingSignals(thread)` | 取得进程与指定线程待处理位的并集。 |
| `takePendingSignal(thread, accepted, action, respectMask)` | 在 accepted 集合中取编号最小信号，默认排除屏蔽位；优先消费线程私有位，返回动作，并执行 ResetHand 复位。 |
| `deliverThreadSignal(threadId, signal)` | 在本 Task 内定位线程；自定义处理器信号挂到该线程，默认动作转入进程级处理。 |
| `deliverSignal(signal)` | 执行进程级挂起、默认忽略、停止、继续或致命动作。 |
| `deliverPendingSignal(context)` | 在用户返回前取待处理信号并安装一个用户处理帧；context 是将恢复的用户现场。 |

发送权限和受监督服务控制由任务服务及发送辅助函数检查。Continue 会先恢复作业控制停止，即使其处理器被屏蔽或忽略；内存恢复冻结仍阻止恢复执行。

默认 WindowChanged、Child、Urgent 被忽略；停止类信号保存各线程原状态并通知父进程，致命默认动作记录信号退出并结束整个 Task。信号打断 Endpoint 等待时返回 Interrupted，具体业务是否重试由调用层处理。

## 用户异常如何进入信号

`raiseException(context, signal, code, address, addressValid, access, debugStatus)` 将 CPU Context 转成用户现场，生成 FaultRecord，并直接尝试在故障线程安装处理帧。同步故障不会只挂成普通进程 pending 位。

| 来源 | 处理 |
| --- | --- |
| 除零、整数溢出 | SIGFPE，保留对应代码和故障现场。 |
| 调试、断点 | SIGTRAP，读取 DR6 区分单步和硬件调试来源；保留硬件给出的 RIP。 |
| 非法指令 | SIGILL。 |
| 可归责的边界、用户栈或一般保护错误 | SIGSEGV；不能确认错误地址时不伪造有效地址。 |
| x87 / SIMD 浮点异常 | 从 FXSAVE/MXCSR 中选取实际未屏蔽异常，产生 SIGFPE。 |
| 对齐检查 | SIGBUS。 |
| 缺页 | 先交 Pager 尝试恢复；不能恢复时，按未映射、权限、文件 EOF 或后端故障选择 SIGSEGV/SIGBUS。 |

只有用户执行或受控 Sys 服务指令范围内的可归责异常进入上述路径。内核自身错误、NMI、双重故障、机器检查及未启用的平台异常按 CPU 应急诊断路径处理，不能一概转成用户信号。

同步故障的处理器缺失、被忽略、信号被屏蔽或帧安装失败时，按该信号的致命动作结束进程。`fatal(signal, fault)` 先记录故障，再统一进入 exitCurrent；不将异常向量冒充退出信号。

## 信号帧和返回

用户 ABI 的 OsSignalInfo、OsMachineContext、OsUserContext 定义在 `include/os-c++/os/syscall.h`。内部 SignalFrame 按 16 字节对齐，包含 magic、前一活动帧地址、用户上下文和信号信息；该结构必须保持约定布局，不能换成任意对象序列化。

`install` 检查处理器与返回跳板确实映射为可执行用户页，选择正常栈或 OnStack 替代栈，保留被中断现场的 128 字节 red zone。最多嵌套 8 层。构造完成后将 RIP 指向处理器，RDI 为信号号，RSI/RDX 为 info/context 地址，并按 NoDefer 和动作掩码更新屏蔽字。

替代栈至少 2048 字节；配置时检查地址范围，投递时才通过用户复制路径确认可写并补齐合法缺页。已经在替代栈内执行时不能替换它。

`restoreSignalFrame(context, userFrame)` 要求 userFrame 是当前最内层活动帧，检查 magic、用户 RIP/RSP、选择子、浮点区指针和 MXCSR 有效位，净化 RFLAGS，恢复浮点现场与掩码，再退回上一活动帧。用户可修改受允许的寄存器和浮点现场，不能借此提升特权。

信号返回使用 task.kernel 的 SignalReturn 请求；验证成功后标记 contextRestored，由统一 IRET 返回路径恢复。没有独立的 SignalReturn syscall 20。

`returnToUser(cpuContext)` 在中断返回用户态时检查信号；系统调用返回也执行相同投递逻辑。因此用户线程即使一直计算、不主动系统调用，也能在可抢占中断返回时接收信号。`captureUserContext` 保存最近用户现场供故障记录和快照使用。

每线程当前使用 512 字节 FXSAVE64；未实现 XSAVE/AVX 扩展现场。处理器进入默认屏蔽浮点环境，信号返回恢复帧内经检查的浮点状态。

## 故障和退出记录

FaultRecord 固定为 96 字节，保存 PID、TID、启动标识、时间、向量、原始错误码、RIP/RSP、信号、代码、地址及有效位、访问类型、DR6 和来源；它按固定格式写入 OSINFO note。

| Task 成员函数 | 功能与参数 |
| --- | --- |
| `recordSignalExit(signal, fault)` | 只记录首次致命信号；fault 可为空，为空时从当前线程补现场，再请求 core。 |
| `getExitInfo(info)` | 返回信号、core 状态和主要故障信息，并检查是否超时。 |
| `recordImage(mapping)` | 记录主程序或共享库的地址区间、文件偏移和路径，最多 64 项。 |
| `recordAuxiliary(headers, count, entry, loader)` | 保存 ELF 程序头、数量、入口、解释器及页大小辅助信息。 |

fork 继承映像登记，exec 提交清除旧登记，由新运行时重新填写。普通退出和信号退出分开保存，内部兼容退出码仍可使用负信号号。

## CoreDump 和快照流程

CoreDump 保存锁、两个私有 Slot、全局策略、票据序号和 watchdog 状态。Slot 保存 Task 引用、快照内存、票据、写者 PID 和是否正在采集。

| 成员函数 | 功能与参数 |
| --- | --- |
| `init()` | 创建独立超时工作线程，每 100 ms 检查快照。 |
| `request(task)` | 在故障路径登记有界请求，不分配或复制整份进程内存。 |
| `pending(task)` | 判断 Requested/Capturing/Ready/Writing 是否仍在进行，过期则设 TimedOut。 |
| `capture(task)` | 在进程停稳后占用快照槽，复制可转储页和寄存器，形成 ELF 内存镜像。 |
| `query(result)` | 复制当前全局策略。 |
| `configure(caller, policy)` | 仅允许 Task 角色更新大小、并发数量、期限和启用状态。 |
| `acquire(caller, ticket)` | 让 Task 写出服务领取一个 Ready 快照并转入 Writing。 |
| `read(caller, ticket, offset, destination, size)` | 核对写者、票据、范围和期限，从快照复制数据；公开服务入口限制单次 4096 字节。 |
| `finish(caller, ticket, success)` | 接受写出结果，设置最终状态，释放槽和 Task 引用并通知父进程。 |
| `expire()` | 释放到期或失败快照，通知父进程；采集中不提前释放缓冲。 |

TaskReclaimer 先停止其他线程、等待所有 CPU 切走并通知设备停止，再 capture，随后即可回收原进程映射，不等待磁盘写完。写者只能按票据读取已复制快照，不能通过票据读取任意 Task 内存。

完整状态流为 Requested → Capturing → Ready → Writing → Complete/Truncated；权限或配额条件可使请求 Skipped，分配或写出失败为 Failed，超过期限为 TimedOut。完成和失败路径均释放引用，不永久阻塞父进程等待。

## 大小、内容和格式

只有默认应产生 core 的致命信号才申请快照。全局未启用、RLIMIT_CORE 为零、凭据条件不允许或进程为受监督服务时跳过。实际大小取全局 maximumBytes 和进程软限额的较小值；小于固定元数据区加一页时跳过。

全局硬上限为每槽 16 MiB、两个槽；线程记录最多 64 个，程序头上限 256。默认策略大小 8 MiB、最多两个、期限 10 秒，但初始 disabled，需写出服务配置启用；可配置期限为 100～60000 ms。

快照只扫描已经驻留且允许导出的用户页，不主动将所有缺页读入。外部映射还需 Maps::dumpAllowed 许可：可信 Pager 管理的普通 RAM 可复制，MMIO、DMA、帧缓冲和未获准共享外部映射不直接读取。达到容量、程序头或现场记录边界时标记截断。

输出是 ELF64、ET_CORE、EM_X86_64。PT_NOTE 保存 Linux x86-64 布局的 NT_PRSTATUS、NT_FPREGSET、NT_SIGINFO、NT_PRPSINFO、NT_AUXV、NT_FILE；故障线程优先排列。OSINFO note 另存 FaultRecord 和截断边界。PT_LOAD 按地址连续且权限相同的用户页合并。

## 写出边界与父进程通知

内核只管理快照、策略和票据。`src/task/core_dump.cc` 读取 /etc/core，执行目录权限检查、磁盘配额和文件发布；maximum_space、directory 由该用户态写出层处理。文件先以排他创建、0600 写入 .partial，同步后通过拒绝覆盖的链接发布最终 core.PID.START.ID。文件系统调用不放到异常入口。

子进程退出通知和 wait 结果等待 core 最终状态或超时。只有 Complete 与 Truncated 设置 WCOREDUMP；Requested、Skipped、Failed、TimedOut 均不代表磁盘上已有成功的 core。写出服务失联时内核 watchdog 仍释放快照，使父进程可以继续回收。
