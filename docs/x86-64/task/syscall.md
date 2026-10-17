# 系统调用（src/kernel/task/syscall/）

本文档描述 `src/kernel/task/syscall/` 的实现：SYSCALL/SYSRET 入口（`entry.S`）、系统调用分发与全部系统调用（`syscall.cc`）、Ring 3 自检探针（`user_probe.S`）。对应头文件 `include/kernel/syscall.h`。

## 1. 调用约定

- 系统调用号：`rax`（从 **1** 起始；0 无效）。编号统一定义在 `include/os-c++/os/syscall.h`。
- 参数：`rdi`、`rsi`、`rdx`、**`r10`**（SYSCALL 占用 `rcx`）、`r8`、`r9`。
- 返回值：`rax`；错误以负 `int64` 返回（`SyscallError`）。

```cpp
enum class SyscallError : int64_t {
    InvalidArgument = -1, InvalidHandle = -2, PermissionDenied = -3,
    NotFound = -4, OutOfMemory = -5, WouldBlock = -6,
    NotSupported = -7, Busy = -8, IoError = -11
};
```

POSIX 库直接使用相同的 OS 系统调用号，不再经过独立的 POSIX 分发路径。

## 2. 初始化与入口

`syscall::init()`（每个 CPU 都要调用，AP 在 `applicationTaskorEntry` 中调用）：

- `IA32_EFER |= 1`（SCE）；
- `IA32_STAR`：内核 CS 基址 `0x08`（位 32-47）、用户选择子基址 `0x10`（位 48-63；SYSRET 时 CS = 0x10+16|RPL3 = 0x23、SS = 0x10+8|RPL3 = 0x1b，与 GDT 布局一致，见 [cpu.md](cpu.md)）；
- `IA32_LSTAR = syscallEntry`；
- `IA32_FMASK` = 位 8|9|10|18（进入时清 TF、IF、DF、AC）。

### 汇编入口（entry.S）

`syscallEntry`：`swapgs` → 用户 RSP 存入 `gs:88`（`syscallUserStack`）→ 从 `gs:48`（`kernelStack`）加载内核栈 → 压栈 18 个 8 字节槽（与 `SyscallContext` 字段顺序一致）→ `rdi = rsp`、`sti`（开中断执行分发）→ `call syscallDispatchEntry` → `cli` → 弹栈 → 恢复用户栈 → `swapgs` → `sysretq`。

```cpp
struct SyscallContext {   // 144 字节
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t userRip, userRflags, userRsp;
};
```

### 分发入口 `syscallDispatchEntry(SyscallContext*)`

1. 校验当前为 User 线程且 `userRip/userRsp < 2^47`，否则杀线程。
2. 特判三个需要完整上下文的调用：
   - **SignalReturn**：`signal::restoreSignalFrame`，失败杀线程；
   - **ForkTask**：见下文；
   - **ReplaceTaskImage**：见下文；成功则直接返回新映像。
3. 其余走 `dispatchSyscall(rax, rdi, rsi, rdx, r10, r8, r9)` 查表。
4. 返回前净化 `userRflags = (userRflags & 0x240dd5) | 0x202`（强制 IF）。
5. 调用 `signal::deliverPendingSignal(context)` 在返回用户态前投递信号（见 [task.md](task.md)）。

### 分发表 `dispatchSyscall`

- 调用号为 0 或 ≥ `Count` → `NotSupported`。
- 否则查 `syscallHandlers[number - OS_SYS_BASE]` 函数指针表。

```cpp
namespace kernel::syscall {
void init();
bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size);
bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size);
uint64_t dispatchSyscall(uint64_t syscallNumber, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6);
extern "C" void syscallEntry();
extern "C" void syscallDispatchEntry(SyscallContext* context);
}
```

## 3. 用户内存访问

```cpp
bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size);
bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size);
```

范围校验（`[0x10000, 2^47)`）+ 地址空间锁 + `memory::validateUserMemory` 页表校验 + 逐字节拷贝。

## 4. 系统调用表

编号以 `include/os-c++/os/syscall.h` 为准，从 1 连续排列；0 无效。PTY 创建和属性控制由 tty 服务 IPC 提供，内核仅提供通用通道。

### 消息队列

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 1 | OpenMessageQueue | name*, flags, maxMessages, msgSize | 名字 ≤63 字符 |
| 2 | UnlinkMessageQueue | name* | — |
| 3 | SendMessageQueue | handle, data*, size, priority | size ≤256；结果 1 → WouldBlock |
| 4 | ReceiveMessageQueue | handle, data*, capacity, priority* | -2 → WouldBlock |
| 5 | GetMessageQueueAttributes | handle, attributes* | — |
| 6 | SetMessageQueueFlags | handle, flags, oldFlags* | 仅允许 0x800 位 |
| 7 | RegisterMessageQueueNotification | handle, type, signal | 占用返回 Busy |

### 进程与 IPC

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 8 | GetStandardHandle | index | 0/1/2 → stdin/stdout/stderr 句柄 |
| 9 | ExitTask | exitCode | 终止整个进程，不返回 |
| 10 | CreateThread | entryPoint, stackPointer, argument | 校验入口可执行、`stackPointer-8` 可写、sp≥8；返回线程 id |
| 11 | ExitThread | exitCode | 终止当前线程，不返回 |
| 12 | Yield | — | 让出 CPU |
| 13 | Sleep | milliseconds | 睡眠（0=让出） |
| 14 | SpawnProgram | program, bootstrapHandle, argumentCount, arguments** | 需 Spawn 能力；加载内置程序（总是与 `libos-c++.so` 动态链接）；≤8 参数、每个 ≤63 字符；返回子进程句柄 |
| 15 | SpawnImage | SpawnImageInfo* | 需 Spawn 能力；映像 ≤8 MiB、≤8 参数、≤3 共享镜像、能力仅允许 Display 位；可指定逻辑父进程与三个标准句柄 |
| 16 | GetBootstrapHandle | — | 返回进程引导句柄 |
| 17 | GetTaskState | taskHandle, exitCode* | 返回 0=运行中、1=已终止（随即回收）、2=已停止 |
| 18 | CloseHandle | handle | 关闭句柄 |
| 19 | GetTaskId | — | 当前 pid |
| 20 | GetThreadId | — | 当前 tid |
| 21 | GetTaskInfo | taskHandle, info* | 写回 {id, state, exitCode, name} |
| 22 | TerminateTask | taskHandle, exitCode | 需 Manage；不可杀自身或内核进程 |
| 23 | SendSignal | taskId, signal | pid>0 指定进程；0 当前进程组；<-1 进程组；-1 不支持 |
| 24 | SendThreadSignal | threadId, signal | 向当前进程内指定线程投递信号 |
| 25 | CreateSession | — | 已是组长则 PermissionDenied |
| 26 | GetTaskIdentifiers | ids* | 写回 {parentId, taskGroupId, sessionId} |
| 27 | SetTaskGroup | groupId | 加入或创建进程组 |
| 28 | SetCredentials | id, uid, gid, effectiveOnly | 需 Users 能力或自身 root；uid/gid < 65535 |
| 29 | QueryTask | id, info* | 按 pid 查询进程信息（含 uid/gid/effectiveUid/effectiveGid/parentId/name） |
| 30 | EnumerateTask | index, info* | 按索引遍历非内核进程 |
| 31 | ForkTask | — | 特判。复制地址空间（`memory::cloneAddressSpaceRoot`）+ 共享内存映射 + 句柄/信号动作继承；子上下文 `rax=0`；返回子 pid |
| 32 | WaitChild | taskId, exitCode*, options | options 位 1=NOHANG、2=UNTRACED；返回子 pid 与 wait status |
| 33 | ReplaceTaskImage | ExecImageInfo* | 特判（exec）。见 [user.md](user.md) |
| 34 | WaitThread | threadId, exitCode*, noHang | 等待同进程线程；0=已退出、WouldBlock、NotFound |
| 35 | WaitAddress | address, expected | 4 字节对齐；*addr ≠ expected → WouldBlock |
| 36 | WakeAddress | address, count | 返回唤醒数 |
| 37 | SetSignalAction | signal, action*, oldAction* | flags 仅允许 NoDefer\|ResetHand\|0x4\|0x10000000；自定义 handler 必须提供合法 restorer |
| 38 | SetSignalMask | how, mask, oldMask* | how：0=并集屏蔽、1=解除、2=直接设置；Kill/Stop 不可屏蔽 |
| 39 | GetPendingSignals | mask* | 进程+线程 pending 合集 |
| 40 | WaitSignal | mask | 非阻塞取一个匹配信号（忽略线程掩码）；无则 WouldBlock |
| 41 | SignalReturn | 帧地址 | 特判，走 `signal::restoreSignalFrame` |
| 42 | TerminalId | value | 读取（value=UINT64_MAX）或设置（0-6）终端 ID |
| 43 | CreateEndpoint | — | 返回句柄（Read\|Write\|Manage） |
| 44 | SendMessage | endpointHandle, message* | 发送（满则阻塞） |
| 45 | ReceiveMessage | endpointHandle, message* | 接收（空则阻塞） |
| 46 | CallService | endpointHandle, request*, reply* | 同步 RPC |
| 47 | ReplyMessage | endpointHandle, reply* | 服务端回复 |

### 管道与通道

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 48 | CreatePipe | handles* | 写回 {readHandle, writeHandle} |
| 49 | ReadPipe | handle, buffer, size | size ≤512；按句柄类型读取管道或双向通道 |
| 50 | WritePipe | handle, buffer, size | size ≤512；按句柄类型写入管道或双向通道 |
| 51 | CreateChannelPair | handles* | 创建一对通用双向通道句柄 |
| 52 | GetChannelIdentity | handle | 查询可跨进程句柄传递保持稳定的通道端标识 |
| 53 | SetChannelNonblocking | handle, enabled | 设置同一通道端的非阻塞读写 |
| 54 | SignalChannelEof | handle | 在通道输出方向排入一次性 EOF 事件 |

### 设备与时间

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 55 | CallPciService | request* | PCI 枚举及配置服务请求，仅可信 Ring 0 pci 服务可调用 |
| 56 | MapDeviceMmio | function*, barIndex, userAddress, size | 返回 MmioRegion 句柄 |
| 57 | AllocateDma | userAddress, info* | 单页 DMA 缓冲 |
| 58 | BindIrq | irq, endpointHandle | irq ≤15；返回 InterruptBinding 句柄 |
| 59 | AcknowledgeIrq | handle | 确认电平触发中断 |
| 60 | MapFramebuffer | userAddress, info* | 映射启动帧缓冲 |
| 61 | GetMonotonicTime | 单调时钟（纳秒） | — |
| 62 | GetWallClockTime | 墙钟时间（纳秒，Unix epoch 起） | — |
| 63 | GetClockResolution | 时钟分辨率（纳秒） | — |
| 64 | GetSystemInfo | info* | 写回 {monotonicTime, totalMemory, freeMemory, onlineCpus, ...} |
| 65 | Log | message*, length | 1..240 字节，输出到串口 |

### 内存

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 66 | MapMemory | address, size, protection | 分配并映射零页；protection 位 0=写、位 1=执行，3（写+执行）拒绝；单次 ≤64 MiB |
| 67 | UnmapMemory | address, size | 解除映射并释放物理页（`pageExternal` 页不释放）；同时释放共享内存映射登记 |
| 68 | ProtectMemory | address, size, protection | 修改保护位；先经共享内存保护检查 |
| 69 | OpenSharedMemory | name*, flags | 返回句柄 |
| 70 | UnlinkSharedMemory | name* | — |
| 71 | ResizeSharedMemory | handle, size | — |
| 72 | MapSharedMemory | handle, address, size, offset, protection | 返回映射地址 |

## 5. Ring 3 自检探针（user_probe.S）

内核启动时把 `.rodata.user_probe` 段的代码映射为 Ring 3 进程执行，端到端验证系统调用路径：

1. `MapMemory`：映射 `0x900000`（4096 字节、可写），校验返回值；
2. 向该页写入魔数 `0x6f73696e666f`（"osinfo"）；
3. `UnmapMemory`：校验返回 0；
4. `Yield`；
5. `ExitTask(0)`。

任一校验失败则以退出码 1 退出并 `ud2`。
