# 系统调用入口与用户现场

实现位于 `src/kernel/task/syscall/`，声明在 `include/kernel/syscall.h`，C++ 命名空间为 `kernel::task`。本页区分跨特权级的六个基础入口、经 IPC 调用的内核服务，以及固定可信映像使用的导入函数。

## 文件分工

| 文件 | 内容 |
| --- | --- |
| `entry.S` | SYSCALL 进入、swapgs、切换内核栈、保存现场，最后通过 IRET 返回。 |
| `syscall.cc` | 初始化 MSR、检查用户地址、复制消息、分派六个调用、绑定当前 SyscallContext。 |
| `services.cc` | 提供 serviceXxx 类型化函数及 serviceResolve 符号白名单，供受控可信映像路径使用。 |
| `user_probe.S` | 内核启动探针使用的用户态短程序。 |

系统调用编号和用户信号 ABI 共享定义位于 `include/os-c++/os/syscall.h`。消息结构来自 `os/ipc.h`，内核服务的业务操作码来自 `include/service/common/kernel-requests.h`。

## 六个基础调用

编号由 RAX 传入，返回值放 RAX；六个参数依次为 RDI、RSI、RDX、R10、R8、R9。没有使用的参数不参与操作。

| 编号 | 操作 | 参数数据 |
| --- | --- | --- |
| 1 | SendMessage | 端点句柄、用户消息地址、deadline、可选取消线程编号。消息地址为 0 且取消编号非零时，执行本进程该线程的端点等待取消。 |
| 2 | ReceiveMessage | 端点句柄、用户接收区地址、deadline。 |
| 3 | CallService | 端点句柄、用户请求地址、用户回复地址、deadline。 |
| 4 | ReplyMessage | 端点句柄、用户回复消息地址；令牌在消息的 replyToken 字段中。 |
| 18 | WaitAddress | 对齐的 32 位用户字地址、expected。 |
| 19 | WakeAddress | 对齐的 32 位用户字地址、最多唤醒数量。 |

OS_SYS_COUNT 是有效入口数量 6，不是最大编号。其他编号返回 NotSupported；句柄操作、线程创建、fork、exec、退出和 SignalReturn 均通过内核服务消息表达。

IPC deadline 是绝对单调纳秒值，UINT64_MAX 为无限，0 为非阻塞。普通端点的排队和内核端点的直接执行有所区别，见 [IPC](ipc.md)。WaitAddress 当前没有期限参数，也没有把这些 IPC deadline 规则扩展到地址等待。

基础入口以负值表示错误。WaitAddress 内部返回“值已不匹配”时映射为 WouldBlock，无效地址或未对齐映射为 InvalidArgument；WakeAddress 返回唤醒数量。业务回复内的错误字段另见 [任务机制服务](service.md)。

## 入口、上下文和返回

`initSyscall()` 为当前 CPU 设置 EFER、STAR、LSTAR 和 FMASK，指定 syscallEntry；进入时屏蔽 TF、IF、DF 和 AC。

`syscallEntry` 先 swapgs，通过 CpuLocalData 保存用户 RSP，切换到当前线程内核栈，再建立 SyscallContext。现场包含 15 个通用寄存器和 userRip、userRflags、userRsp，共 18 个 64 位字；其顺序和汇编栈布局必须一致。CpuLocalData 中的栈偏移由 `kernel/cpu.h` 的共享常量提供。

`syscallDispatchEntry(context)` 校验当前线程类型及用户 RIP/RSP，绑定线程的 syscallContext，并记录最近用户现场。随后按寄存器参数调用 `dispatchSyscall(number, arg1, ..., arg6)`。

普通返回将结果写回 RAX，净化 RFLAGS 并检查待投递信号；`userContextReturn` 把现场尾部转换成 IRET 帧，恢复寄存器、swapgs、iretq。这样不会借助 SYSRET 再次覆盖 RCX/R11，也可恢复信号返回需要的标志组合。

上下文结构属于固定汇编 ABI，不能因 C++ 成员整理改变字段顺序或字节数。

## 用户地址与消息复制

| 函数 | 功能与参数 |
| --- | --- |
| `copyFromUser(kernelBuffer, userBuffer, size)` | 校验用户范围和读权限，经 PagerBridge 准备合法缺页，再在地址空间锁内复制到内核缓冲。 |
| `copyToUser(userBuffer, kernelBuffer, size)` | 校验写权限并准备用户页；写入继续遵守 COW 和页表权限。 |
| `resolveEndpoint(handle, rights)` | 在当前 Task 表中建立 HandleTable::Pin，校验端点类型和所需权限。 |

普通用户范围从 0x10000 到低半区上界 0x0000800000000000，拒绝长度为零、加法溢出和越界范围。旧 Sys 特权路径另有直映范围兼容分支，不是普通应用可用地址。

消息先完整复制进内核，不能让服务在分派期间直接使用会被用户改写的消息头。ServiceCall 先读取操作码决定缓冲和上下文路径，完整复制后再次核对操作码，拒绝两次读取不同的请求。

ReceiveMessage 和 CallService 在领取消息前准备可写回复区。若结果已经安装了接收句柄，最终复制到用户区失败时关闭这些新增句柄，避免泄漏引用。ReplyMessage 的令牌来自复制后的消息，不能依赖服务线程“最近一次请求”。

## 修改执行上下文的服务调用

`Endpoint::handlesContext(operation)` 识别 Task 的退出、创建线程、让出、休眠、fork、替换映像、标准句柄及信号返回请求。这些操作在真实调用线程内通过 callImmediate 执行，不排入普通 RPC 请求队列。

`ServiceCall::normal` 使用常规局部消息缓冲。`ServiceCall::context` 为 fork、exec 和 SignalReturn 在堆上分配请求/回复缓冲，并通过 ThreadCleanup 登记释放，避免深层处理占用过多内核栈。两条路径都由 complete 完成地址验证和结果复制。

fork 预先把子进程零结果写入将被克隆的回复区，再建立子现场；父进程正常收到子 PID。exec 成功及 SignalReturn 成功都会标记 contextRestored，此时 complete 不写旧回复区，syscallDispatchEntry 也不再覆盖恢复后的寄存器。

退出成功后不会返回；exec 准备失败仍可正常写回错误。不能将这些操作搬到一个无关工作线程执行，再通过伪造当前 Task 来处理用户上下文。

## 初始句柄

句柄是当前 Task 表中的引用编号。加载器先安装真实对象，再将编号写入用户启动栈辅助向量：

| 辅助向量 | 数据与使用 |
| --- | --- |
| OS_AUX_KERNEL_HANDLE | 内核 IPC 初始端点。可请求创建端点、取得其他内核服务句柄、操作自身句柄表。 |
| OS_AUX_REGISTRY_HANDLE | 用户态服务注册中心端点。用于查找 fs、storage 等服务；init 早期可没有它。 |

`CoreServices::bootstrap` 保证内核初始句柄指向真实 IPC 服务且可保留。`os::Runtime::prepare` 解析辅助向量，运行时查询读取缓存，不需要 GetBootstrapHandle 系统调用。注册中心初始句柄与内核初始句柄不能混用。

修改用户内存里的编号不会新增对象引用或权限。持内核服务端点也不会自动获得管理员身份，各服务继续检查真实 caller 的角色、能力和资源权限。

## 可信导入函数

`services.cc` 保留 serviceSend、serviceReceive、serviceCall、serviceReply、serviceWaitAddress、serviceWakeAddress，以及句柄、线程、内存和设备相关类型化函数。`serviceResolve(symbol)` 按固定符号名返回明确列出的函数地址；存在一个 serviceXxx 定义不等于它已列入导出白名单。

这是受控可信映像的兼容路径，不是 Ring3 应用可直接调用的内核地址 API。普通应用仍通过 libos 的原生 IPC/系统调用入口。加载器只为获准映像处理这类绑定，构建时导入审计读取当前 services.cc 白名单。

新增业务请求通常扩展具体内核服务，不增加 syscall 编号。新增可信导入则同时涉及加载约束、白名单和函数签名，不能只在某个头文件声明后就视为对所有程序开放。
