# Ring 3 系统调用 ABI

系统是混合内核。唯一 syscall 指令入口服务于 Ring 3；所有服务进程均为 Ring3，Ring0 只保留基础机制和 .kernel 对象。

编号以 include/os-c++/os/syscall.h 为唯一来源，0 无效，Count=20（有效调用 19 个）。内核枚举、POSIX 头文件和运行时引用同一份定义。

## 当前编号

| 号 | 调用 | 保留原因 |
|---|---|---|
| 1 | SendMessage | IPC 发送；也承载同进程线程取消子操作 |
| 2 | ReceiveMessage | IPC 领取及句柄事务提交 |
| 3 | CallService | 同步 RPC、绝对截止时间 |
| 4 | ReplyMessage | 以 Reply Token 回复 |
| 5 | GetStandardHandle | 三个标准流句柄 |
| 6 | GetBootstrapHandle | 引导句柄、端点工厂及驻留服务发现子操作 |
| 7 | CloseHandle | 进程句柄表关闭及对象引用释放 |
| 8 | MapMemory | 底层地址空间映射机制 |
| 9 | UnmapMemory | 底层解除映射与同步 TLB 回收 |
| 10 | ProtectMemory | 页权限保护机制 |
| 11 | ExitTask | 当前进程退出，不返回 |
| 12 | CreateThread | 安装当前进程的新线程入口与栈 |
| 13 | ExitThread | 当前线程退出，不返回 |
| 14 | Yield | 让出 CPU |
| 15 | Sleep | 定时阻塞 |
| 16 | ForkTask | 捕获调用者完整寄存器、FPU 与信号上下文 |
| 17 | ReplaceTaskImage | 替换调用者地址空间及返回上下文 |
| 18 | WaitAddress | futex 比较与阻塞的原子衔接 |
| 19 | WakeAddress | futex 唤醒热路径 |

参数为 rdi/rsi/rdx/r10/r8/r9，号及返回值为 rax。错误为负 int64，具体值见 os::ErrorCode。IPC deadline 使用绝对单调纳秒；UINT64_MAX 无限，0 非阻塞。

## v0.1.1 映射入口

编号保持不变。`MapMemory(address, size, flags, handle, offset, target)` 的 flags 低两位沿用 Write=1、Execute=2；类型取 `OS_MAP_ANONYMOUS=0`、`OS_MAP_PAGE_OBJECT=0x100` 或 `OS_MAP_SHARED_OBJECT=0x200`。匿名模式要求 handle/offset 为 0。页对象/共享对象按句柄类型和读写权限校验，W^X 约束不变。

`UnmapMemory(address, size, target)` 与 `ProtectMemory(address, size, protection, target)` 的 target=0 表示自身。其他进程目标必须同时满足固定 `ServiceRole::Memory` 与 `taskCapabilityMemory`，普通进程即使是 root 也不能操作其他进程的地址空间。Protect 的 `OS_PROTECT_COPY_ON_WRITE=4` 仅允许该 MM 身份设置。

这些机制复用 `MappingOperations`、`PageMappings` 和 `SharedMemory`，memory.kernel 删除对应映射 RPC；旧操作编号留空，不重用。VMA、延迟分页、文件 backing 和 COW 策略仍位于 Ring3 MM。修改 SDK 接口后需重建关联程序和服务。
