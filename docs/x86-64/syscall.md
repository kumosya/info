# 系统调用（src/kernel/task/syscall/）

本文档描述 `src/kernel/task/syscall/` 的实现：SYSCALL/SYSRET 入口（`entry.S`）、系统调用分发与全部 73 个调用（`syscall.cc`）、Ring 3 自检探针（`user_probe.S`）。对应头文件 `include/kernel/syscall.h`。

## 1. 调用约定

- 系统调用号：`rax`。
- 参数：`rdi`、`rsi`、`rdx`、**`r10`**（SYSCALL 占用 `rcx`）、`r8`、`r9`。
- 返回值：`rax`；错误以负 `int64` 返回（`SyscallError`）。

```cpp
enum class SyscallError : int64_t {
    InvalidArgument = -1, InvalidHandle = -2, PermissionDenied = -3,
    NotFound = -4, OutOfMemory = -5, WouldBlock = -6,
    NotSupported = -7, Busy = -8, IoError = -11
};
```

## 2. 初始化与入口

`initSyscall()`（每个 CPU 都要调用，AP 在 `applicationProcessorEntry` 中调用）：

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
   - **SignalReturn（56）**：`restoreSignalFrame`，失败杀线程；
   - **ForkProcess（70）**：见下文；
   - **ReplaceProcessImage（72）**：见下文；成功则直接返回新映像。
3. 其余走 `dispatchSyscall(rax, rdi, rsi, rdx, r10, r8, r9)` 查表。
4. 返回前净化 `userRflags = (userRflags & 0x240dd5) | 0x202`（强制 IF）。
5. 调用 `deliverPendingSignal(context)` 在返回用户态前投递信号（见 [task.md](task.md)）。

## 3. 用户内存访问

```cpp
bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size);
bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size);
```

范围校验（`[0x10000, 2^47)`）+ 地址空间锁 + `validateUserMemory` 页表校验 + 逐字节拷贝。

## 4. 系统调用表（73 个）

### 进程/线程（0-4、49、68、70、71、72）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 0 | ExitProcess | exitCode | 终止整个进程，不返回 |
| 1 | CreateThread | entryPoint, stackPointer, argument | 校验入口可执行、`stackPointer-8` 可写、sp≥8；返回线程 id |
| 2 | ExitThread | exitCode | 终止当前线程，不返回 |
| 3 | Yield | — | 让出 CPU |
| 4 | Sleep | milliseconds | 睡眠（0=让出） |
| 49 | WaitThread | threadId, exitCode*, noHang | 等待同进程线程；0=已退出、WouldBlock、NotFound |
| 68 | SendThreadSignal | threadId, signal | 向当前进程内指定线程投递信号 |
| 70 | ForkProcess | — | 特判。复制地址空间（`cloneAddressSpaceRoot`）+ 共享内存映射 + 句柄/信号动作继承；子上下文 `rax=0`；返回子 pid |
| 71 | WaitChild | processId, exitCode*, options | options 位 1=NOHANG、2=UNTRACED；返回子 pid 与 wait status |
| 72 | ReplaceProcessImage | ExecImageInfo* | 特判（exec）。见 [user.md](user.md) |

### 内存（5、6、63）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 5 | MapMemory | address, size, protection | 分配并映射零页；protection 位 0=写、位 1=执行，3（写+执行）拒绝；单次 ≤64 MiB |
| 6 | UnmapMemory | address, size | 解除映射并释放物理页（`pageExternal` 页不释放）；同时释放共享内存映射登记 |
| 63 | ProtectMemory | address, size, protection | 修改保护位；先经共享内存保护检查 |

### IPC 端点（7-11）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 7 | CreateEndpoint | — | 返回句柄（Read\|Write\|Manage） |
| 8 | SendMessage | endpointHandle, message* | 发送（满则阻塞） |
| 9 | ReceiveMessage | endpointHandle, message* | 接收（空则阻塞） |
| 10 | CallService | endpointHandle, request*, reply* | 同步 RPC |
| 11 | ReplyMessage | endpointHandle, reply* | 服务端回复 |

详见 [ipc.md](ipc.md)。

### 进程加载（12、13、30）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 12 | SpawnProgram | program, bootstrapHandle, argumentCount, arguments** | 需 Spawn 能力；加载内置程序（总是与 `libos-c++.so` 动态链接）；≤8 参数、每个 ≤63 字符；返回子进程句柄 |
| 13 | GetBootstrapHandle | — | 返回进程引导句柄 |
| 30 | SpawnImage | SpawnImageInfo* | 需 Spawn 能力；映像 ≤8 MiB、≤8 参数、≤3 共享镜像、能力仅允许 Display 位；可指定逻辑父进程与三个标准句柄 |

详见 [user.md](user.md)。

### 调试（14）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 14 | Log | message*, length | 1..240 字节，输出到串口 |

### 设备访问（15-20、27、28、29）

均需 `processCapabilityDevice`（MapFramebuffer 需 Display 能力）。

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 15 | EnumeratePci | functions*, capacity | capacity ≤64；返回枚举数量 |
| 16 | ReadPciConfig | function*, offset, value* | 32 位配置读 |
| 17 | WritePciConfig | function*, offset, value | 32 位配置写 |
| 18 | MapDeviceMmio | function*, barIndex, userAddress, size | 返回 MmioRegion 句柄 |
| 19 | AllocateDma | userAddress, info* | 单页 DMA 缓冲 |
| 20 | BindIrq | irq, endpointHandle | irq ≤15；返回 InterruptBinding 句柄 |
| 27 | ReadIoPort | port, value* | 8 位，仅 PS/2 端口（0x60/0x64） |
| 28 | WriteIoPort | port, value | 8 位 |
| 29 | MapFramebuffer | userAddress, info* | 映射启动帧缓冲 |

详见 [device.md](device.md)。

### 进程信息（21-24、34-36、45、46）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 21 | GetProcessState | processHandle, exitCode* | 返回 0=运行中、1=已终止（随即回收）、2=已停止 |
| 22 | CloseHandle | handle | 关闭句柄 |
| 23 | GetProcessId | — | 当前 pid |
| 24 | GetThreadId | — | 当前 tid |
| 34 | GetStandardHandle | index | 0/1/2 → stdin/stdout/stderr 句柄 |
| 35 | GetProcessInfo | processHandle, info* | 写回 {id, state, exitCode, name} |
| 36 | TerminateProcess | processHandle, exitCode | 需 Manage；不可杀自身或内核进程 |
| 45 | CreateSession | — | 已是组长则 PermissionDenied |
| 46 | GetProcessIdentifiers | ids* | 写回 {parentId, processGroupId, sessionId} |

### 时间（25、26、47）

| 号 | 名称 | 说明 |
|---|---|---|
| 25 | GetMonotonicTime | 单调时钟（纳秒） |
| 26 | GetWallClockTime | 墙钟时间（纳秒，Unix epoch 起） |
| 47 | GetClockResolution | 时钟分辨率（纳秒） |

### 管道/伪终端（31-33、37-43）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 31 | CreatePipe | handles* | 写回 {readHandle, writeHandle} |
| 32 | ReadPipe | handle, buffer, size | size ≤512；按句柄类型自动分派 Pty/Pipe |
| 33 | WritePipe | handle, buffer, size | size ≤512；同样分派 |
| 37 | CreatePty | handles* | 写回 {masterHandle, slaveHandle} |
| 38 | GetPtyAttributes | handle, attributes* | |
| 39 | SetPtyAttributes | handle, attributes* | |
| 40 | GetPtyWindowSize | handle, size* | |
| 41 | SetPtyWindowSize | handle, size* | |
| 42 | SetPtyForegroundGroup | handle, groupId | |
| 43 | SetPtyNonblocking | handle, enabled | |

### 信号（44、52-56）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 44 | SendSignal | processId, signal | pid>0 指定进程；0 当前进程组；<-1 进程组；-1 不支持 |
| 52 | SetSignalAction | signal, action*, oldAction* | flags 仅允许 NoDefer\|ResetHand\|0x4\|0x10000000；自定义 handler 必须提供合法 restorer |
| 53 | SetSignalMask | how, mask, oldMask* | how：0=并集屏蔽、1=解除、2=直接设置；Kill/Stop 不可屏蔽 |
| 54 | GetPendingSignals | mask* | 进程+线程 pending 合集 |
| 55 | WaitSignal | mask | 非阻塞取一个匹配信号（忽略线程掩码）；无则 WouldBlock |
| 56 | SignalReturn | 帧地址 | 特判，走 `restoreSignalFrame` |

### futex（50、51）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 50 | WaitAddress | address, expected | 4 字节对齐；*addr ≠ expected → WouldBlock |
| 51 | WakeAddress | address, count | 返回唤醒数 |

详见 [sync.md](sync.md)。

### 消息队列（57-62、69）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 57 | OpenMessageQueue | name*, flags, maxMessages, msgSize | 名字 ≤63 字符 |
| 58 | UnlinkMessageQueue | name* | |
| 59 | SendMessageQueue | handle, data*, size, priority | size ≤256；结果 1 → WouldBlock |
| 60 | ReceiveMessageQueue | handle, data*, capacity, priority* | -2 → WouldBlock |
| 61 | GetMessageQueueAttributes | handle, attributes* | |
| 62 | SetMessageQueueFlags | handle, flags, oldFlags* | 仅允许 0x800 位 |
| 69 | RegisterMessageQueueNotification | handle, type, signal | 占用返回 Busy |

### 共享内存（64-67）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 64 | OpenSharedMemory | name*, flags | 返回句柄 |
| 65 | UnlinkSharedMemory | name* | |
| 66 | ResizeSharedMemory | handle, size | |
| 67 | MapSharedMemory | handle, address, size, offset, protection | 返回映射地址 |

### POSIX 占位（48）

| 号 | 名称 | 参数 | 说明 |
|---|---|---|---|
| 48 | PosixUnsupported | operation | 上报未实现的 POSIX 类别，一律返回 NotSupported，见 [user.md](user.md) |

## 5. Ring 3 自检探针（user_probe.S）

内核启动时把 `.rodata.user_probe` 段的代码映射为 Ring 3 进程执行，端到端验证系统调用路径：

1. `MapMemory`：映射 `0x900000`（4096 字节、可写），校验返回值；
2. 向该页写入魔数 `0x6f73696e666f`（"osinfo"）；
3. `UnmapMemory`：校验返回 0；
4. `Yield`；
5. `ExitProcess(0)`。

任一校验失败则以退出码 1 退出并 `ud2`。
