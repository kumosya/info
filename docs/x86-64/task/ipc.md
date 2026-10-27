# 内核 IPC 与句柄

实现位于 `src/kernel/task/ipc/`，声明在 `include/kernel/ipc.h` 和 `task.h`，命名空间为 `kernel::task`。内核提供端点通信、身份确认和对象引用传递；管道及消息队列的缓冲区和业务状态由 Ring3 的 `src/task/pipe/`、`src/task/msgq/` 维护。

## 文件与对象

| 文件或类 | 作用 |
| --- | --- |
| `ipc.cc` / Endpoint | 消息发送、接收、同步调用、回复、取消、超时和中断通知。 |
| EndpointImplementation | 仅在 ipc.cc 定义，保存端点锁、消息环形队列、等待链、活动调用、访问授权和关闭状态。 |
| `handle.cc` / HandleTable | 各进程自己的句柄表和对象引用管理。 |
| HandleTable::Pin | 校验类型和权限后持有临时对象引用，保护一次内核操作期间的对象生命期。 |
| HandleTable::Transfer | 捕获消息携带的引用，并在真正接收时安装到目标进程。 |
| `core_service.cc` / CoreServices | 保存内核服务端点，为进程创建相应句柄。 |
| `service.cc` | IPC 内核服务请求处理，操作实际调用者的句柄表。 |
| EndpointService | 保存具体处理函数，call 传入实际 Task 和请求；没有虚函数分派。 |

## 消息和身份

`IpcMessage` 使用 `os::IpcMessage` 的固定 ABI，定义在 `include/os-c++/os/ipc.h`。消息包含 type、senderPid、senderTid、replyToken、length、handleCount、515 个 64 位数据字，以及 4 组句柄、权限和传递选项。length 的单位是数据字，不是字节。

`validateMessage` 检查长度和句柄数量；`stampSender` 以当前 CPU 的真实 Task/Thread 写入发送者身份。服务不能信任客户端自行填写的 PID。句柄只在所属进程表中有意义，不能把发送者的句柄编号直接拿到接收者中使用。

## 句柄表和引用

句柄的低 32 位是槽位索引加一，高部保存 16 位 generation；generation 为 0 会跳过。槽位重新使用时改变 generation，旧编号不能访问后来装入的对象。初始表有 64 槽，容量不足时增长。

每个槽保存对象类型、权限、标志和共享 Reference。Reference 在最后一个引用释放时按对象类型释放资源。设备资源仍受设备所有者规则约束，不能把通用引用计数当作硬件访问授权。

| 成员函数 | 功能与参数 |
| --- | --- |
| `init(capacity, owner, escrow)` | 分配初始表；owner 指定所属 Task，escrow 表示恢复域的托管引用表。 |
| `add(object, type, rights)` | 为对象新建句柄；返回 0 表示失败。 |
| `addAt(handle, object, type, rights)` | 在指定句柄位置安装，供需要保持编号的继承路径使用。 |
| `get(handle, type, requiredRights)` | 在表锁内校验并取得裸指针；不持有离开锁后的独立引用。 |
| `duplicate(handle, rights, flags)` | 在本表增加指向同一 Reference 的句柄，权限不能扩大。 |
| `copyTo(destination, handle, destinationHandle)` | 将引用复制到目标表；可指定保持的目标编号。 |
| `restrictRights(handle, rights)` | 将权限缩小为原权限的子集。 |
| `setFlags(handle, flags)` | 设置支持的 NoInherit、CloseOnExec 标志。 |
| `inspect`、`inspectAt` | 按句柄或槽位读取对象、类型和权限，供受控遍历与继承使用。 |
| `close(handle)` | 移除一个槽并释放相应引用。 |
| `closeOnExec()` | 关闭带 CloseOnExec 标志的句柄。 |
| `closeAll()` | 关闭表中现有句柄。 |
| `dispose()` | 禁止继续安装、关闭全部句柄并释放表内存。 |

查询接口还涉及容量、句柄标志、引用数和对象权限。跨锁、跨阻塞操作应使用 `Pin(table, handle, type, rights)`；它通过 ThreadCleanup 登记引用，即使线程被终止也能释放。

fork 跳过 NoInherit 句柄，其他可继承句柄尽量保留原编号；exec 执行 CloseOnExec。kernelHandle、bootstrapHandle 和三个 standardHandles 的 Task 访问方法也在 handle.cc，前两者分别代表内核 IPC 初始端点和用户态注册中心端点。

## 句柄传递的提交点

`Transfer::capture(source, message, commitMove)` 先验证所有源句柄、权限和选项，再捕获引用。允许复制或 move，权限只可收缩。MMIO、DMA、IRQ、设备域和帧缓冲映射句柄不允许经普通消息转交。

`Transfer::install(destination, message)` 检查目标容量，准备完成后安装全部句柄，并将消息中的编号改为接收方编号。跨表操作按表地址顺序加锁，避免相反方向传递时锁顺序颠倒。

- Send/Call 在成功入队时提交 move：源槽被关闭，队列取得引用；入队前失败保留源句柄。
- Receive 才把队列引用安装到真正领取消息的 Task，不预装到端点所有者表中。
- Reply 先捕获但不关闭 move 源槽，目标容量和源引用确认后，在安装阶段提交。
- `clear()` 释放尚未安装的引用，用于取消、关闭和失败清理。

同一消息中的句柄安装不会留下部分成功结果。消息入队后的取消不会替调用者恢复已经提交的 move。

## 普通端点通信

| 成员函数 | 功能与参数 |
| --- | --- |
| `Endpoint::create(service)` | 创建端点；service 为空时使用用户态队列，非空时绑定内核处理函数。 |
| `send(message, deadline)` | 复制消息并入队，不建立等待回复的请求。 |
| `receive(message, deadline)` | 优先领取合并的 IRQ 通知，再领取普通消息；成功时安装携带的句柄。 |
| `call(request, reply, deadline)` | 发送带唯一回复令牌的请求并等待回复。 |
| `reply(message)` | 按 replyToken 查找活动请求，验证领取进程身份，再传递回复。 |
| `cancel(threadId)` | 取消本进程指定线程在该端点上的发送、接收或调用等待。 |
| `requestActive(token)` | 查询回复令牌是否仍有效，供延迟回复的服务释放过期请求资源。 |
| `destroy()` | 关闭端点，唤醒等待者并清理队列引用；内存释放仍由引用生命期决定。 |

普通队列有 8 个 FIFO 槽。队列满时发送者等待，队列空时接收者等待。等待登记与条件检查在端点锁内完成，释放锁后阻塞，避免丢失唤醒。

deadline 是绝对单调纳秒值：UINT64_MAX 表示无限，0 表示不能等待。已有消息或空槽可立即完成；不能立即完成时返回 WouldBlock，有限期限到达返回 Timeout。内核服务使用下面的直接执行路径，不能把普通队列语义一概套用。

同步调用为每个请求分配唯一 replyToken。接收时记录领取 Task 和 Thread；同一 Task 可由另一线程回复，但错误进程、重复、取消后或迟到回复会被拒绝。领取线程死亡会使尚未回复的调用以 PeerDied 完成。

## 内核服务调用

`CoreServices::init()` 建立 Memory、Device、System、Task、Pci 和 Ipc 的内核端点。`getHandle(caller, kind)` 只为 caller 安装可调用句柄；取得端点本身不代表拥有所有服务权限。

`bootstrap(caller)` 检查现有 kernelHandle 是否确实指向 IPC 内核端点；可复用时清除其继承限制，否则重新安装。加载器将该句柄写入启动辅助向量，详见 [系统调用](syscall.md)。

内核端点的 `callImpl` 直接调用 `EndpointService::call(caller, request, reply)`，不唤醒另一个服务线程。一般内核服务调用仍登记取消和超时状态；需要修改当前执行上下文的 Task 操作由 `handlesContext` 识别，经 `callImmediate` 执行，不挂普通请求队列。直接执行过程本身不会被截止时间事务回滚。

内核 IPC 服务支持 CreateEndpoint、GetCoreService 和 Handle。Handle 子操作包括关闭、复制、权限收缩、标志、关闭状态、引用数、端点身份、销毁端点及请求有效性查询，操作的是实际 caller 的表，没有独立的句柄管理 syscall。

## 超时、信号和死亡清理

| 函数 | 功能 |
| --- | --- |
| `expireWaits(now)` | 完成到期等待，并清理引用数已经归零的端点内存。 |
| `interruptThread(thread)` | 将该线程的端点等待或调用标记 Interrupted，供信号投递使用。 |
| `cancelThread(thread)` | 从各端点和定时链移除线程登记；取消其调用，处理它领取但未回复的请求。 |
| `closeOwner(owner)` | 进程回收时撤销端点所有权；恢复保护的端点按恢复规则保留身份。 |
| `receiverClosed(owner)` | 处理接收引用丢失，使依赖该接收者的请求结束。 |

常见结果为 WouldBlock、Timeout、Cancelled、Interrupted、EndpointClosed 和 PeerDied。已被用户态服务领取的请求取消后，不会回滚该服务已经完成的业务操作，也不会自动重放。

等待记录和回复目标可能位于线程内核栈。强制终止必须先完成远端停稳，再取消登记，最后释放栈，不能只把线程标成 Terminated 就释放它。

## 中断通知和恢复端点

`notifyFromInterrupt(type, value)` 对 IRQ 类型 0x495251、编号 0～15 使用 pendingIrqs 位图；同一路 IRQ 多次到达会合并，不占普通消息槽，也不表示逐次中断计数。其他通知使用普通槽，队满时失败。硬件确认与重开由设备的 InterruptBinding 负责。

`protectRecovery(owner)` 将端点纳入服务恢复保护；旧实例退出时保留可接管的端点身份。`adoptRecovery(owner)` 由同角色新实例取得接收所有权，`releaseRecovery(owner)` 解除保护。恢复域托管与序列校验见 [任务机制服务](service.md)。
