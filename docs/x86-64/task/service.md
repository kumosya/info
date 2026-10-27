# task.kernel 任务机制服务

`src/kernel/task/service/` 提供内核驻留的任务机制，声明在 `include/kernel/task.h`，命名空间为 `kernel::task`。它通过 EndpointService 接收消息，在真实调用线程内执行，不创建替调用者工作的独立服务进程。

用户态 `src/task/` 负责程序启动请求、服务监督等策略；这里负责建立和切换执行上下文、检查任务权限、等待状态以及资源生命期。两者通过既有 IPC 接口衔接。

## 文件和入口

| 文件或类 | 内容 |
| --- | --- |
| `service.cc` / `taskKernelService()` | 返回绑定 TaskService::call 的 EndpointService。 |
| `service.cc` / `TaskService::call(caller, request, reply)` | 校验当前线程确实属于 caller，检查消息长度，再按 TaskOperation 分派。 |
| `service.cc` / TaskImages | 以 caller 为资源来源读取启动映像和参数，启动子进程。类只在本文件可见。 |
| `mechanisms.cc` / TaskMechanisms | 保存 caller 引用，实现当前调用上下文的线程创建、fork 和 exec。 |
| `recovery.cc` / ServiceRecovery | 按受监督服务角色保存恢复数据与托管对象。 |

请求操作码定义在 `include/service/common/kernel-requests.h`，传输记录在 `include/service/common/records.h`。这些固定布局用于消息传输，Task 和 Thread 的内部数据仍由各自对象持有。

普通请求至少包含 4 个数据字；具体操作继续检查长度和范围。通信或权限失败通常由处理函数返回负的 ErrorCode；部分机制结果放在 reply.data[0]，POSIX 凭据、优先级和限额的正 errno 放在 reply.data[1]。调用方必须遵循具体请求格式，不能把成功送达等同于业务成功。创建的对象句柄通过 reply.handles 返回。

## 当前线程的执行机制

| 请求 | 实现与主要输入 |
| --- | --- |
| ExitTask / ExitThread | 调用 exitCurrent 或 exitCurrentThread，输入退出码；成功后不返回。 |
| CreateThread | TaskMechanisms::createThread(entry, stack, argument)，输入用户入口、用户栈顶和首参数，返回线程编号。 |
| Yield / Sleep | 调用 yieldThread 或 sleepCurrentThread，Sleep 输入毫秒数。 |
| Fork | TaskMechanisms::fork，复制调用者资源和当前用户现场，父进程得到子 PID，子进程得到零。 |
| ReplaceImage | TaskMechanisms::replace，输入调用进程中的 ExecImageInfo 地址，成功后进入新映像。 |
| StandardHandle | 读取调用进程的标准句柄，输入索引 0～2。 |
| Identity / ThreadIdentity | 返回当前进程或线程编号。 |
| SignalReturn | 输入当前活动信号帧地址，恢复当前用户现场；非法帧按 SIGSEGV 终止。 |

CreateThread 先通过 PagerBridge 补齐入口和栈顶附近的合法用户页，再建立不可运行的 Thread，配置完成后入队。它要求 caller 就是当前进程。

Fork 禁止拥有设备资源域的进程执行，避免复制硬件映射。它先向子进程将继承的回复区写零，克隆页表、普通和共享映射、句柄与凭据，再同步用户态 MM 状态，最后开放子线程执行。失败的子 Task 交给回收器销毁。

ReplaceImage 的准备阶段限制主映像和每个共享映像最大 8 MiB，最多 4 个共享映像，参数和环境分别最多 64 项，每项最多 383 字节，运行时状态最多 16 KiB。字符串和指针必须从调用者地址空间复制，不能保留指向旧映像的指针进入提交后阶段。

commitExec 的终止、停稳和换根过程见 [任务管理](task.md)。成功后 markContextRestored 使系统调用返回层跳过旧回复区和普通返回值写入；SignalReturn 使用同样的衔接方式。

## 启动其他程序

SpawnProgram 和 SpawnImage 分别对应固定引导映像与调用者提供的映像，均由 TaskImages 处理。

`TaskImages::spawnProgram(program, bootstrapHandle, argumentCount, userArguments)` 按固定 ProgramId 启动引导映像。program 指定映像，bootstrapHandle 指定注册中心，userArguments 是调用者中的参数指针数组。Init 可启动固定程序；Task 角色仅可启动 PipeServer 和 MessageQueueServer，Device 角色仅可启动 PciServer 和 StorageServer。

`TaskImages::spawnImage(userInfo)` 读取 SpawnImageInfo 并调用 loadProgramImage。调用者必须有 Spawn 能力；映像、共享库和参数来自调用者地址空间，可指定逻辑父进程、标准句柄、终端关联和新会话标志。

输入 capabilities 必须为零，实际服务能力由允许的角色计算。创建受监督服务要求调用者为 Task 角色、逻辑父进程为 Init，并显式设置 managed 标志，防止应用自称系统服务。

两个启动入口成功后都在调用者表中安装 Task 句柄，并在回复句柄数组中返回。该句柄编号不是 PID。

## 查询、句柄控制和等待

| 请求 | 处理方法 |
| --- | --- |
| OpenTask | 输入 PID，0 为自身；返回带读权限的 Task 句柄，root、自身和直接父进程还可获得管理权限。 |
| State / Info | 输入 Task 句柄，要求读权限；查询简要运行状态或完整 TaskInfo。State 对已退出目标还等待资源销毁完成。 |
| Query / Enumerate | 按 PID、链表位置或最小编号查找 Task，再由 describe 生成信息。 |
| ThreadInfo | 输入 Task 编号和最小 TID，返回下一个存活线程信息。 |
| Terminate | 输入带管理权限的 Task 句柄和退出码，终止其他普通进程并等待资源回收；拒绝自身、kernel 和受监督服务。 |
| ControlSignal / ControlTerminate | 面向受监督服务的管理路径，要求 Init 或有角色的直接父监督者，并要求管理句柄；ControlSignal 只允许请求 SIGTERM。 |
| WaitChild / WaitId | 按 PID 或进程组等待子进程，包含非阻塞、停止、继续及 waitid 不消费事件选项。 |
| WaitThread | 在调用进程内等待 TID，可指定非阻塞。 |

describe 组合凭据、父进程、PGID/SID、终端前台组、运行时间、主线程 nice 和存活线程数。命令行仅对 root 或有效 UID 匹配的调用者公开。

等待循环调用 Task::waitForChild 或 waitForThread；返回需要等待时，通过 blockWaitingThread 阻塞后重新检查。SIGCHLD 与完成事件竞争时，先尝试领取已完成的子进程事件，再决定返回 Interrupted，避免丢弃实际完成结果。

## 凭据、会话和优先级

| 请求 | 处理方法 |
| --- | --- |
| Credentials | 查询凭据或改变当前凭据、附加组；跨进程读取限 root、Fs 或 Users 角色。 |
| SetCredentials | 内核受控设置 UID/GID；跨进程操作要求 Users 能力，其余按自身身份规则检查。 |
| Authentication | 查询认证记录；其他进程的身份只能由获准的 Users 或受监督身份观察服务读取。 |
| AuthenticateSession | 仅持 Users 能力的 Users 服务可为目标进程认证，支持保留当前凭据。 |
| CreateSession / Identifiers / SetGroup | 创建会话、读取父 PID/PGID/SID，或设置自身及直接子进程的同会话进程组。 |
| SetTerminalForeground | Console 角色只能设置 console 终端，Tty 角色只能设置 PTY 的前台进程组。 |
| Priority | 按线程、组或 UID 查询及设置 nice，或对当前线程执行相对 nice。 |
| ResourceLimit / CoreLimit | 查询和设置软硬限额；目前只落实 CORE 和 NICE。 |

身份修改、认证及继承规则见 [凭据](credentials.md)，nice 和限额的参数及权限见 [资源](resource.md)。

## 信号、用户内存和 core

| 请求 | 处理方法 |
| --- | --- |
| SendSignal | 正 PID 指定进程，0 指定当前进程组，小于 -1 指定组；不实现 -1 广播。信号 0 只检查存在性。普通信号路径禁止控制受监督服务。 |
| SendThreadSignal | 只向调用进程自己的线程发送；输入 TID 和信号号。 |
| SignalAction | 读取或安装进程信号动作，校验标志、处理函数和返回跳板；拒绝捕获 Kill/Stop。 |
| SignalMask | 对当前线程执行加入、移除或替换屏蔽字，始终清除 Kill/Stop 位。 |
| PendingSignals / TakeSignal | 查询进程与线程 pending 并集，或从指定信号集合取出一个信号。 |
| SignalStack | 读取或设置当前线程替代信号栈；正在栈内时不能替换，启用大小至少 2048 字节。 |
| SignalGeneration | 读取当前线程已经安装的用户信号处理帧计数，供等待信号的运行时使用。 |
| ProbeMemory | 校验并通过 PagerBridge 准备当前进程指定范围，输入地址、长度和是否写入。 |
| ImageMapping / Auxiliary | 登记当前映像文件区间、偏移、路径与 ELF 辅助信息，供 core 输出。 |
| ExitInfo / CorePolicyQuery | 读取获准目标的退出故障信息，或读取全局 core 策略。 |
| CoreConfigure / CoreAcquire / CoreRead / CoreFinish | Task 角色配置策略、领取票据、分段读取快照并报告写出结果。CoreRead 服务入口每次最多 4096 字节。 |

信号处理帧、寄存器检查和快照生命期见 [信号与 core](core.md)。信号返回通过 TaskOperation::SignalReturn 完成，不占新的系统调用编号。

## 服务恢复域

ServiceRecovery 按 ServiceRole 分域，保存当前所有者、epoch、sequence、完整快照、待提交缓冲、增量日志和托管句柄表。call 要求 caller 为有固定角色的受监督服务，且消息至少 5 个数据字；Acquire 以外操作必须匹配当前 owner 和非零 epoch。

| 请求 | 功能与主要输入 |
| --- | --- |
| RecoveryAcquire | 领取本角色恢复域，返回 epoch、序列、快照大小及日志信息；已有完整快照时返回只读 PageObject 句柄。 |
| RecoveryPrepare | 输入字节数和快照/追加模式，预分配提交目标；不发布新状态。 |
| RecoveryCommit | 输入源 PageObject 句柄、大小和预期 sequence，复制完整快照，增加序列并清除旧增量日志。 |
| RecoveryAppend | 使用已准备的追加缓冲，复制一条增量记录并增加序列。 |
| RecoveryReadLog | 按记录下标取得只读页对象、字节数与记录序列。 |
| RecoveryPut | 托管 PageObject、SharedMemory 或受恢复保护的 Endpoint，返回恢复域内 token。 |
| RecoveryGet | 按 token 将对象引用复制给新实例；端点同时接管接收身份。 |
| RecoveryDrop | 按 token 和预期序列移除托管引用，端点解除保护。 |
| RecoveryList | 从指定槽位枚举 token、下一位置和类型。 |
| RecoveryGeneration | 输入目标 PID，读取仍有效的地址空间代数。 |

换所有者前确认旧实例已经终止且资源回收完成，随后增加 epoch 并丢弃旧的未提交缓冲；活着的实例占有恢复域时返回 Busy，尚未回收完返回 WouldBlock。旧 epoch 不能继续提交。

完整快照最大 4 MiB，增量日志按实际分配页计最多 4 MiB；最多 128 条日志，单条最多 64 KiB，托管对象最多 16384 个。恢复数据保存在内核内存，不等同于掉电持久化。

## 内存冻结

`Task::setMemoryFrozen(frozen)` 将普通目标进程的可执行线程停止，记录停止前状态；冻结时等待远端 CPU 离开目标。它拒绝 kernel、当前进程、受监督服务和已终止目标。解除冻结只恢复未被作业控制停止的线程。

首次冻结设置 20 秒截止时间。`claimMemoryFreezeExpiry(now)` 只领取一次到期事件；恢复 watchdog 每 250 ms 检查，超时则终止冻结进程，避免永久挂起。内存冻结不替代设备停止、页表代数检查或完整的服务业务恢复协议。
