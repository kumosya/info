# 内核任务管理

`src/kernel/task/` 管理进程对象、线程、调度、IPC、同步和退出后的资源清理。这里的 C++ 声明位于 `include/kernel/task.h`、`ipc.h`、`sync.h` 和 `syscall.h`，均在 `kernel::task` 命名空间内。汇编入口保留 C 链接名称。

内核保存能够执行任务所必需的状态；程序启动策略、服务监督、管道和消息队列的业务处理位于用户态。内核 `task.kernel` 的实现见 [任务机制服务](service.md)，用户态 task 服务见 [任务管理服务](../../service/task/task.md)。

## 源文件分工

| 文件 | 实现内容 |
| --- | --- |
| `task.cc` | Task 创建、全局任务表、父子关系、地址空间根、fork 资源继承、exec 提交、进程组与会话。 |
| `thread.cc` | 引导线程登记、线程创建、首次运行入口、内核栈和浮点状态切换。 |
| `wait.cc` | 等待子进程、等待线程、子进程状态通知，以及 join 与线程对象回收的衔接。 |
| `exit.cc` | 当前进程和线程退出、终止其他线程、提交销毁、向父进程报告退出。 |
| `reclaim.cc` | TaskReclaimer 工作队列和线程；实际释放线程栈、句柄、映射及页表。 |
| `signal.cc` | 信号动作、待处理信号、停止与继续、用户信号帧、异常投递与信号返回。 |
| `sched/rbtree.cc` | 运行队列红黑树的插入、删除、旋转和修复。 |
| `sched/cfs.cc` | nice 权重、虚拟运行时间、线程入队、出队、亲和性和迁移入口。 |
| `sched/schedule.cc` | 每 CPU 调度器、阻塞和唤醒、休眠、时钟处理、切换完成与远端停稳。 |
| `sched/switch.S` | 保存和恢复内核执行现场、首次进入用户态、恢复 fork 用户上下文。 |
| `ipc/handle.cc` | 句柄表、对象引用、临时固定引用、句柄传递及 Task 的启动和标准句柄。 |
| `ipc/ipc.cc` | Endpoint 消息队列、请求与回复、等待、取消、超时和中断通知。 |
| `ipc/core_service.cc` | CoreServices 创建内核服务端点，为调用进程安装初始端点和具体服务句柄。 |
| `ipc/service.cc` | 内核 IPC 服务：创建端点、取得内核服务、操作调用者的句柄表。 |
| `credentials/credentials.cc` | 用户与组凭据、附加组、认证会话以及能力位操作。 |
| `resource/resource.cc` | core/nice 资源限额、线程信息、进程或线程优先级请求。 |
| `core/core_dump.cc` | 故障记录、ELF core 内存快照、写出票据和超时回收。 |
| `service/service.cc` | task.kernel 请求分派、映像启动、查询、等待和权限检查。 |
| `service/mechanisms.cc` | 当前调用线程的创建线程、fork 和替换映像机制。 |
| `service/recovery.cc` | 受监督服务的恢复域、快照、增量记录、对象托管和内存冻结。 |
| `sys/service.cc` | system.kernel 的时间、系统信息与串口日志请求。 |
| `sync/sync.cc` | 内核等待队列、互斥锁、信号量、一次初始化，以及线程等待和清理登记。 |
| `sync/wait.cc` | 按用户地址等待与唤醒。 |
| `syscall/syscall.cc` | 六个系统调用的分派、用户数据复制、服务调用缓冲区和上下文返回处理。 |
| `syscall/entry.S` | SYSCALL 入口、内核栈切换、寄存器保存及统一 IRET 返回。 |
| `syscall/services.cc` | 固定可信映像使用的内核导入函数和符号白名单。 |
| `syscall/user_probe.S` | 由内核启动探针进入用户态时使用的短汇编程序。 |

调度细节见 [调度器](sched.md)，通信和句柄见 [IPC](ipc.md)，锁与等待见 [同步](sync.md)，异常现场和快照见 [信号与 core](core.md)。

## Task、Thread 和 AddressSpace

`Task` 表示进程，保存地址空间、句柄表、父子关系、线程链表、凭据、能力、会话、信号动作和退出信息。`Thread` 是调度单位，保存所属进程、内核栈、用户执行现场、CPU 亲和性、运行时间、等待登记和浮点状态。一个 Task 可以有多个 Thread；独立内核工作线程允许没有所属 Task。

`AddressSpace` 保存页表根物理地址、代数和锁：

| 成员函数 | 功能与参数 |
| --- | --- |
| `init(rootPhysicalAddress)` | 登记页表根，初始化锁，代数从 1 开始。 |
| `replaceRoot(rootPhysicalAddress)` | 替换根并返回旧根；根发生变化时增加代数，跳过 0。调用者负责加锁、切换 CR3 和释放旧根。 |

读取接口涉及页表根、代数和锁。代数用于区分同一 Task 在 exec 前后的地址空间，防止旧映射对象对新地址空间执行清理。

`TaskState` 包括 Created、Running、Terminated、Stopped。`Task::state()` 在进程仍存活且被作业控制停止时返回 Stopped。`ThreadState` 包括 Created、Ready、Running、Blocked、Sleeping、Stopped、Terminated。进程停止与内存恢复冻结分别保存，SIGCONT 不能解除内存恢复冻结。

## 创建与查找

| 函数 | 功能与参数 |
| --- | --- |
| `allocateId()` | 从原子计数器取得任务或线程编号；主线程挂入 Task 时采用该 Task 的编号。 |
| `initThreads()` | 为当前 CPU 已在执行的引导现场建立 Thread 对象。 |
| `initScheduler()` | 为当前 CPU 建立运行队列和 idle 线程，配置定时器。 |
| `init()` | 建立 kernel Task，关联引导线程，初始化回收、core、设备资源域和服务恢复。 |
| `Task::create(name, parent)` | 新建页表根、Task 和初始 64 槽句柄表，继承父进程凭据及限额，加入父子链和全局任务表。name 最多保存 63 字节；parent 可为空。 |
| `Task::setInitTask(task)` | 登记接收孤儿进程的 init，并保留其引用；由启动路径调用。 |
| `find(taskId)` | 按任务编号查找并返回 TaskReference。 |
| `at(index)` | 按全局链表位置查找，跳过 kernel Task。 |
| `next(minimumId)` | 查找编号不小于 minimumId 的最小任务，供按编号遍历使用。 |
| `Task::parentReference()` | 在读取父进程指针时取得引用，供离开锁后继续使用。 |

`getCurrent()`、`currentThread()` 分别读取当前 CPU 的 Task 和 Thread。`getIdle()` 返回 kernel Task；每 CPU 的 idle 线程由运行队列另行保存。

`TaskReference` 不可复制、可以移动。构造时保留 Task 引用，并将释放动作加入当前线程的 ThreadCleanup 链；正常析构或线程被清理时释放引用。裸指针不能代替跨锁、跨阻塞路径上的 TaskReference。对象引用存活只保证 Task 对象仍在，不保证其原地址空间仍可访问。

## 线程创建和首次运行

| 成员函数 | 功能与参数 |
| --- | --- |
| `Thread::create(task, entryPoint, argument, type, makeRunnable)` | 分配线程与内核栈，建立首次切换栈帧；task 指定所属进程，entryPoint 和 argument 指定内核入口，type 指定执行类型，makeRunnable 控制是否立即入队。 |
| `Thread::createUser(task, entryPoint, userStackPointer, argument, initialStackSize, initialAffinity, makeRunnable)` | 建立用户线程；设置用户入口、栈、首参数和 CPU 掩码。initialStackSize 用于旧 Sys 入口的初始栈复制，普通用户栈由地址空间准备。 |
| `Thread::createForkedUser(task, context, signalMask)` | 保存 fork 子进程的用户现场和掩码，将返回寄存器置零，从专用入口恢复用户执行。 |
| `Task::attachThread(thread)` | 将线程挂入进程链；首个线程设为主线程，更新存活线程数。进程停止接受新线程时失败。 |
| `Thread::bootstrap()` | 完成首次上下文切换收尾；User 进入 Ring3，Kernel 执行入口并在返回后退出，Sys 使用保留的特权入口。 |
| `Thread::switchTo(next)` | 保存当前线程的浮点状态，恢复 next 的浮点状态，再调用汇编切换内核栈。 |

普通内核栈为 64 KiB；Sys 线程或有固定服务角色的任务线程使用 256 KiB。默认时间片为 4 ms。用户态服务正常使用 User 线程，`ThreadType::Sys` 是保留路径，不能据此把所有系统服务描述为内核态运行。

每线程保存 512 字节 FXSAVE64 状态；内核 C++ 仅使用通用寄存器，切换时保存的浮点状态属于用户线程。初始栈帧必须与 `switchThreadContext` 的寄存器恢复顺序一致。

## fork 和 exec

`TaskMechanisms::fork()` 处理当前调用线程的 fork：先在用户回复区写入子进程的零结果，创建子 Task，复制页表和映射引用，再调用 `inheritForFork(parent)` 复制句柄、凭据、会话、限额及信号动作。完成用户态 MM 的 VMA/COW 状态克隆后，才创建可运行的子线程。拥有设备资源域的进程禁止 fork，防止硬件映射脱离所有者。

`TaskMechanisms::replace(image, failure)` 读取 ExecImageInfo，复制参数和环境，准备新 ELF、共享映像、运行时状态和用户栈。image 是调用进程中的描述地址；failure 接收准备失败时的错误。

`Task::commitExec(newRootPhysicalAddress, name, currentThread)` 执行提交：关闭新线程接纳，终止并摘除其他线程，等待远端 CPU 切走，停止设备资源，取消旧等待和临时引用，释放旧映射，切换页表根，关闭 CloseOnExec 句柄并销毁旧根。当前线程成为唯一主线程；捕获型信号动作恢复默认，忽略型动作保留。不可逆提交段禁止抢占，保持中断可用，不能在其中睡眠。

exec 准备失败时保留原映像。提交成功后改写当前 SyscallContext，并标记 contextRestored，系统调用返回路径直接进入新映像，不向旧回复地址写入数据。

## 等待、退出与回收

`wait.cc` 的等待函数只检查状态并登记等待；真正阻塞和重试由 task.kernel 的请求处理循环完成。

| 成员函数 | 功能与参数 |
| --- | --- |
| `waitForChild(taskId, waiter, status, options, childId, event)` | 等待匹配的子进程。taskId 为 -1 时任意子进程、正数时指定 PID、0 时同进程组、小于 -1 时指定组；waiter 是当前等待线程，status 和 childId 返回结果，event 可接收 waitid 事件。 |
| `waitForThread(threadId, waiter, exitCode, noHang)` | 等待同进程线程，拒绝等待自身和重复消费；noHang 表示不能立即取得结果时不登记等待。 |
| `notifyChildStateChange()` | 摘下并唤醒父进程的子进程等待链。 |
| `retireJoinedThread(thread)` | 记录目标线程内核栈已退役；join 结果也被消费后才释放 Thread 对象。 |
| `reapJoinedThreadLocked(thread)` | 在 Task 锁内摘除满足回收条件的线程对象，必要时更新主线程指针；它不负责释放仍在使用的栈。 |
| `notifyThreadExit(thread)` | 唤醒 join 等待者，只计一次线程退出；最后线程退出时将进程标记终止并提交回收。 |
| `terminateOtherThreads(exitCode)` | 拒绝新线程，终止除当前线程外的线程，更新线程计数并请求远端调度。 |
| `destroy()` | 向 TaskReclaimer 提交清理并等待资源回收完成；拒绝销毁 kernel Task 和当前 Task。 |
| `TaskReclaimer::submit(task)` | 只提交一次，队列独立持有 Task 引用；提交者退出不会丢失回收责任。 |

等待函数返回 0 表示有结果，1 表示已登记、需要阻塞，2 表示非阻塞且暂无事件，-1 表示目标不存在或不能等待。子进程选项包括 NOHANG、停止和继续事件；waitid 另外支持事件筛选与不消费事件。

正常退出状态为 `(exitCode & 255) << 8`；信号退出保留信号号，只有 core 写出 Complete 或 Truncated 才置 core 标志；停止为 `(signal << 8) | 0x7f`，继续为 `0xffff`。core 仍在处理时延迟向父进程报告最终退出结果。

`exitCurrent(code)` 先结束同进程其他线程，再进入 `exitCurrentThread(code)`。线程退出取消 IPC、执行临时引用清理、登记退出，并切换到其他线程；不能在自己的栈上释放当前内核栈。

`TaskReclaimer::run()` 调用 `Task::destroyResources()`：

1. 禁止新线程，标记剩余线程终止，调用 `quiesceTask` 等待远端 CPU 完成切换。
2. 通知设备资源域停止访问，必要时采集 core 快照。
3. 将地址空间根置零，撤销端点所有权，取消各线程等待并释放 ThreadCleanup。
4. 将子进程转交 init；init 不可用时转交 kernel Task。
5. 释放线程、句柄表、普通映射、共享映射和旧页表，通知设备地址空间已释放。
6. 发布资源回收完成状态，必要时通知父进程，再释放队列持有的引用。

resourceState_ 的 0、1、2 分别表示未提交、已提交和已回收。正常退出的 Task 对象保留退出信息，直到父进程 wait 从父子链与全局表摘除；尚未启动线程便失败的 Created Task 由回收器直接摘除。

`unlinkGlobal()` 是 Task 的私有方法，在全局任务表锁内摘除当前对象。wait 和回收器复用它，不直接访问全局链表变量。

## 锁与资源生命期

全局任务表锁保护查找和链表修改，Task 自身的锁保护进程状态与线程、父子关系等数据，AddressSpace 的锁保护页表根和映射访问。需要离开全局锁继续操作任务时，先取得 TaskReference；访问句柄对象时使用 HandleTable::Pin。

调度队列锁保证线程状态与队列操作衔接，不能替代地址空间锁。终止标志、远端 CPU 停稳、设备停止、地址空间释放是不同步骤：对象仍有引用不表示旧页表仍有效，CPU 切走也不表示 DMA 已经停止。

`quiesceTask` 等待时不能持 Task 锁、地址空间锁或运行队列锁。处理大范围释放、端点取消和设备回收时，按当前实现先离开保护任务状态的短临界区，再进入可能耗时的清理路径。

## 进程组、会话与终端

`setTaskGroup(groupId)` 将进程放入同一会话的进程组，0 表示以自身编号作为组号。`createSession()` 拒绝已有进程组组长，成功后令 SID 和 PGID 都等于自身编号，并解除终端关联。已存在的认证会话标识随新会话调整。

`terminalForeground(kind, index)` 和 `setTerminalForeground(kind, index, group)` 保存终端前台进程组，不管理窗口或像素尺寸。kind 区分 console 终端和 PTY；具体输入输出与作业控制策略由对应用户态服务处理。

`sendSignalToTask` 和 `sendSignalToTaskGroup` 根据实际调用者身份校验发送权限；signal 为 0 时只检查目标存在性。向组投递时分批取得 TaskReference，在释放全局表锁后逐个投递，避免持全局锁进入可能阻塞或退出的路径。
