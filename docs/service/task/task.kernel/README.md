# task.kernel：任务、身份和信号机制

独立实现 `src/kernel/task/service/service.cc`；端点组装在 `src/kernel/task/core_service.cc`。这是 Ring0 驻留 EndpointService，不是独立进程，没有启动 PID、文件程序或自己的调度线程。init 将其发布为 `task.kernel`；调用在既有 IPC syscall 内完成。

## 机制与协议

`os::TaskOperation` 覆盖 Identity、ThreadIdentity、Query、Info、State、Wait/WaitThread、SpawnProgram/SpawnImage、Terminate、信号与凭据、会话和进程组。公共客户端为 `os::Task`；所有数值 PID 查询与能力句柄操作分别验证。

| 类别 | 验证与资源 |
| --- | --- |
| Query/内部 Enumerate | 内核任务列表引用；内部单调 PID 游标供 FS 构造 procfs |
| Info/State/Wait | TaskHandle 及 Read 权限；等待队列可取消，退出状态与停止状态区分 |
| SpawnProgram | Init 或合法 Task/Device 上级；只加载 builtin ProgramId |
| SpawnImage | 用户指针、文件大小、参数和共享映像边界；逻辑父进程与受控角色授权 |
| Terminate | Manage 能力；普通入口禁止对 managedService 操作 |
| ControlSignal/ControlTerminate | Init 或真实服务 owner；只服务管理端可用，优雅信号限定 SIGTERM |
| SendSignal | UID 权限、有效目标；正 PID 和组发送都跳过受保护服务 |
| 凭据修改 | Users 能力或现有调用者权限；root 不等于拥有设备能力 |

公共 listTask、TaskListEntry 和 Ring3 task 的旧 List/Kill 请求已移除，不能以别名恢复。`/proc` 的 task 元数据在同一 describe 路径生成，含会话、控制 TTY、启动时间、累计 CPU 和受监护标记。

Info/Query/内部枚举对其他有效 UID 的调用者清空命令参数，仅 root 或相同有效 UID 可见；不能通过直接 TaskInfo 查询绕过 procfs 的 cmdline 权限。非 root 控制服务与外部参数保护由 tests/service-runtime.cc 的真实降权子进程验证。

## 执行与锁

HandleTable::Pin 在调用期间固定对象；TaskReference 保护列表查询得到的任务生命周期。地址空间修改用原有地址空间锁，任务列表及队列用中断安全锁。组信号先获取有界对象引用批次，释放全局任务列表锁再投递，不能持全局锁阻塞或终止调用者。

fork/exec、线程上下文和映射继续使用已有 syscall，不在本对象再实现第二套业务；ELF 加载实现位于 `src/kernel/user/`。服务身份在开始调度前写入，fork 普通子任务不自动取得服务标记。

线程 join 消费退出状态一次。记录回收同时要求 join 已完成、CPU 已切离旧栈且没有未完成 IPC/清理引用；等待线程和上下文切换的完成路径在任务锁下汇合，避免回收仍在执行的内核栈或留下无限累积的线程记录。创建线程先缓存 TID 再入队，防止远端线程快速结束并被 join 后，创建入口仍解引用旧指针。

`tests/kernel-lifecycle.h` 在一个持续存活的任务内重复创建、join 128 个远端线程，每轮确认只剩锚线程记录并验证第二次 join 被拒绝；另覆盖 32 轮中断回收请求者及 64 轮远端 CR3/exec 与 kill 竞争。通过 OSINFO_LIFECYCLE_KERNEL 选择隔离测试内核，主系统不包含测试逻辑。

## 故障与诊断

本对象随内核驻留，systemctl 不列为可停止任务。非法句柄、越界、权限和死亡对象返回原生 ErrorCode；libc 负责 errno 适配。故障回收和内核异常终止不走普通 kill 的保护检查，因此仍能清理崩溃服务。
