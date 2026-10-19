# OS 运行时

lib/os/runtime.cc 提供 Ring 3 syscall 封装及对象化客户端。所有服务进程运行于 Ring3，通过已有 IPC 与内核机制对象通信。内核内部直接调用具名类方法。

直接调用保留为 19 个基础 syscall 的薄包装，其编号见[ABI](../../x86-64/task/syscall.md)。其余接口由类提供：

| 类 | 职责 |
|---|---|
| Process | 进程查询、加载、等待、凭据、进程组、信号和线程身份 |
| Terminal | 终端 ID、读写、切换和前台进程 |
| Handle | RAII、复制、权限缩减、标志与端点状态 |
| Memory | memory 驻留机制服务、页对象及共享对象 |
| VirtualMemory | mm 服务的匿名、文件、共享映射及区域操作 |
| Pipe / MessageQueue | Ring3 pipe/msgq 服务协议 |
| Clock / System | 时间、系统信息及日志 |
| Runtime | 初始化、析构、内存与字符串工具 |
| Pty / File / ServiceRegistry | PTY、文件及服务发现 |

Terminal::id_ 保存进程运行时终端身份，不新增 TerminalId syscall；exec 参数和运行时状态传递终端选择。Process::getThreadId 走 task.kernel；Process::signalReturn 使用调用线程绑定的完整 syscall 上下文，借用当前 syscall 入口绑定到 Thread 的 SyscallContext，恢复后设置 contextRestored，分发器不覆盖恢复的 rax。

Memory::allocateMemory 的基础堆用于引导及服务内部 eager 分配。libc mmap 的区域、拆分、权限和 backing 由 Ring 3 MM 管理；不能混用低层 map/unmap 与 MM 区域接口后期待元数据自动修复。

IpcMessage 含 senderPid、senderTid、replyToken、515 个内联字、最多 4 个句柄及逐句柄 rights/options。同步回复必须传入领取请求的 Token。deadline 为绝对单调纳秒；取消复用现有入口。

普通程序 start.S 调用 Runtime 初始化及 C++ 构造，main 返回后由 libc exit 执行退出回调、析构和描述符关闭。服务入口使用 service_start.S，生命周期逻辑位于应用对象中；C ABI 入口桥是链接约定。

启动栈使用 argc/argv/envp 与 ELF 辅助向量。Runtime 从辅助向量取得共享库析构入口，在主程序析构之后执行；libc 从自定义辅助项取得 exec 描述符和工作目录状态。普通动态程序由独立 [Ring3 链接器](../../x86-64/dynamic-linker.md) 加载，依赖不再固定为四个库。[Directories](directories.md) 提供数据、状态、配置、缓存、日志和运行目录的类方法。
