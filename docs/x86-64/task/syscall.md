# Ring 3 系统调用 ABI

系统是混合内核。唯一 syscall 指令入口服务于 Ring 3；所有服务进程均为 Ring3，Ring0 只保留基础机制和 .kernel 对象。

编号以 include/os-c++/os/syscall.h 为唯一来源，0 无效，Count=20（有效调用 19 个）。内核枚举、POSIX 头文件和运行时引用同一份定义。

## 当前编号

| 号 | 调用 | 保留原因 |
|---|---|---|
| 1 | SendMessage | IPC 发送；也承载同进程线程取消子操作 |
| 2 | ReceiveMessage | IPC 领取及句柄事务提交 |
| 3 | CallService | 同步 RPC、绝对截止时间 |
| 4 | ReplyMessage | 以显式 Reply Token 回复 |
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
