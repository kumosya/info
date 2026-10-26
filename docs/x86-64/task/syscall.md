# Ring 3 系统调用与内核服务

系统调用入口只保留六个操作。应用库将句柄、内存映射和任务机制编码为消息，通过CallService访问内核服务端点；这些服务在调用者当前的内核执行路径上运行，不需要唤醒用户态服务线程。

## 六个基础入口

| 编号 | 调用 | 功能 |
|---|---|---|
| 1 | SendMessage | 发送消息及取消指定线程的端点等待 |
| 2 | ReceiveMessage | 接收消息并提交句柄传递 |
| 3 | CallService | 同步调用用户态服务或内核服务 |
| 4 | ReplyMessage | 按回复令牌返回结果 |
| 18 | WaitAddress | 原子衔接用户地址比较与阻塞 |
| 19 | WakeAddress | 唤醒等待该用户地址的线程 |

保留原有编号，其余旧编号返回NotSupported。OS_SYS_COUNT表示有效入口数量6，不表示最大编号。参数使用rdi、rsi、rdx、r10、r8、r9，编号及返回值使用rax；返回用户态使用IRET。错误值见os::ErrorCode。IPC截止时间采用绝对单调纳秒，UINT64_MAX表示无限等待，0表示非阻塞请求。

## 启动句柄

句柄是当前进程句柄表中的引用编号，不是内核地址。加载器先在进程句柄表中建立引用，再将编号写入启动栈辅助向量。

- OS_AUX_KERNEL_HANDLE：内核IPC服务的初始端点。提供创建端点、取得其他内核服务端点及句柄表操作。
- OS_AUX_REGISTRY_HANDLE：父进程传入的服务注册中心端点。ServiceRegistry通过它按名称查找fs、storage等用户态服务；init起初可以没有这个端点，由自身建立注册中心。

Runtime::prepare在解释器、静态程序及服务运行时初始化阶段解析辅助向量。getBootstrapHandle读取缓存中的注册中心编号，不执行系统调用。ServiceTransport取得内核服务端点时使用初始内核端点，不依赖用户态名称注册，因此不会为了取得第一个通信端点而递归依赖IPC。

初始内核端点不代表管理员权限。服务从实际调用者读取身份，继续检查角色、能力、句柄类型和权限。不能通过修改辅助向量中的编号取得额外权限。fork继承进程句柄表，exec建立新启动栈时重新确认初始内核端点有效且能够跨exec保留。

## 服务方法

| 内核服务 | 请求与实现 |
|---|---|
| IPC | IpcOperation::CreateEndpoint创建端点；GetCoreService取得具体服务端点；Handle按真实调用者句柄表执行关闭、复制、权限收缩、标志及引用查询 |
| Memory | MemoryOperation::Map、Unmap、Protect复用MappingOperations、PageMappings和SharedMemory，校验对象权限与目标进程能力 |
| Task | 创建线程、退出进程或线程、让出CPU、休眠、标准句柄查询、fork、exec及信号返回 |

映射flags的低位为Write=1和Execute=2，映射类型仍为OS_MAP_ANONYMOUS、OS_MAP_PAGE_OBJECT、OS_MAP_SHARED_OBJECT。跨进程映射仍要求固定Memory角色及能力；COW标记也保留原权限限制。VMA、文件后备、延迟分页和COW策略继续由用户态mm服务管理。解除映射或取消写权限时，不能破坏当前同步调用的回复缓冲区。

## 上下文完成方式

任务服务通过TaskMechanisms操作创建线程、fork与exec。请求必须来自当前真实调用线程，不通过工作线程伪装调用者身份。

fork先将零返回值写入调用者回复缓冲区，再复制地址空间、资源和用户上下文。子进程从原IPC调用返回零，父进程收到新任务编号；写回复保留正常的用户内存及COW检查。

exec先构建新地址空间及启动栈，全部准备成功后提交。成功时替换当前系统调用保存的用户上下文并标记contextRestored，IPC返回路径跳过旧回复地址，也不覆盖新的寄存器。失败保留原映像并正常回复错误。

SignalReturn验证信号帧、用户地址和寄存器状态后恢复上下文，同样标记contextRestored。libc信号返回跳板保存原始帧地址，调整调用栈对齐后调用原生客户端。非法信号帧终止进程。

退出进程或线程成功后不返回。此类机制调用不挂入普通RPC等待队列。fork、exec和信号返回的消息缓冲区由内核临时存储承载，随线程清理机制释放，避免深层上下文操作耗尽内核栈。
