# 任务管理服务

Ring3 实现位于 `src/task/main.cc`，注册名 `task`、`control.task`，角色 Task，能力 Spawn。init 创建它；task 创建并监护 [pipe](pipe/README.md)、[msgq](msgq/README.md)，两个子服务就绪后才注册 task 的业务端点。服务源码使用 flat 遍历，子目录 main 不会被链接进父镜像。

## 请求协议

`os::TaskRequest` 保留 Wait=2、Execute=3，新增 ExecuteService=6。旧 Spawn/List/Kill 请求已删除；全系统进程发现使用 `/proc`，不维护第二份应用进程表。

| 请求 | 内容 | 结果 |
| --- | --- | --- |
| Execute | 绝对文件路径、参数、registry 与可选标准流句柄 | 状态及一个 TaskHandle |
| ExecuteService | 相同负载；真实发送者必须具有 Init 角色 | 带受监护服务标记的任务句柄 |
| Wait | 一个 TaskHandle | state 与 exitCode；0 运行、1 结束、2 停止或等待被中断 |

Execute 的 data[0] 低 32 位是参数个数；40/41/42 位标识 stdin/stdout/stderr，句柄按 registry、stdin、stdout、stderr 排列。32–39 位携带文本终端编号。data[1..] 为路径及参数的 NUL 字符串序列；最多 64 个参数，总字节数不超过 `(ipcDataWordCount-1)*8`，解析同时受 request.length 约束。程序名、PID、TaskHandle 含义不同。

## 加载与资源

文件要求是非空普通文件，最大 8 MiB。读取采用独立完成端点、深度 8 的异步窗口；确认大小后分配缓冲，全部失败路径应关闭文件、输入能力和映像缓冲。解析 ELF 的 PT_INTERP；普通动态程序只预载解释器，DT_NEEDED 由 Ring3 链接器处理。没有解释器的固定角色服务使用引导运行库完成重定位；运行库来源地址空间与主映像的任务地址空间分别记录。

SpawnImage 的逻辑父进程来自 request.senderPid。内核在调度前写入参数、会话、TTY、角色和受监护标记。返回句柄通过回复转移后关闭 task 本地副本；创建方或服务 owner 持有自己的句柄并负责回收。

主线程串行加载磁盘程序，生命周期线程处理 pipe/msgq 的控制与监护。读取 ELF 的异步通知不会与业务请求端点混用。

## 权限

普通 Execute 不接受能力或服务角色。只有 Init 发起的 ExecuteService 可请求受控服务加载；Task 角色也不能单靠路径代表普通调用者创建特权程序。固定 users/tty/console/guiw 路径由 task 识别角色，内核再次验证调用链与能力上限。普通 root、同名文件和自行传 role 都不能绕过检查。

非内置通用服务使用 None 角色，但仍有 managedService 标记和服务信号保护。旧 ProgramId 文件加载回退已禁用。Ring0 任务对象见 [task.kernel](task.kernel/README.md)，系统对象见 [system.kernel](system.kernel/README.md)。

## 故障与诊断

v0.1.3 增加独立 core writer 线程 `src/task/core_dump.cc`，从 `/etc/core` 读取策略，仅通过 task.kernel 票据读取已冻结复制的有界快照，复用 FS 服务写出 ELF core。它不占用程序加载请求端点；转储失败或超时不阻止进程回收。

异常分类、信号 ABI、转储权限及实际验证记录见[用户态异常与 core](../../x86-64/task/exceptions-and-core.md)。
