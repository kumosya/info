# 混合内核重构记录

执行规格：[hybrid-kernel-refactor-prompt.md](hybrid-kernel-refactor-prompt.md)。

## 初始核查

| 项目 | 源码证据 | 初始结论 |
|---|---|---|
| P0 | `ipc.cc` 的 `enqueueMessage` 在检查权限、容量前调用 `transferHandles`，逐个安装且不回滚 | 存在；同时把 owner 当成接收方 |
| P1 | `Endpoint::reply` 按 `server == Thread::getCurrent()` 查找 | 存在；缺少显式请求关联 |
| P2 | 调度器 `prepareCurrentWait/blockCurrentThread/wakeThread` 已用运行队列锁保护 pending-wake；RPC 等待未使用同一准备协议 | 部分存在；不能把已有 pending-wake 保护说成完全缺失 |
| P3 | `Task::destroy` 释放地址空间但不释放 Task/句柄表数组；`waitForChild` 移除任务表后不释放对象 | 存在；裸指针依赖要求先建立所有权再回收 |
| P4 | `commitExec` 标记远端线程终止、发送重调度 IPI，随后立即销毁旧根 | 存在；重调度请求没有完成确认 |

## 初始服务和调用边界

- 内置镜像：init、task、fs、pci、device、storage；仅 pci/device/storage 当前设置 SystemService。
- 磁盘服务：tty、console、guiw、users。现有服务总览未同步 Ring 0 执行身份。
- 普通用户经 SYSCALL；可信服务目前经运行时 CS 分支执行 int 0x80，仍进入通用 syscall 分派。
- 服务注册表嵌入 init；启动顺序：注册表 → pci → task/device/storage/fs → users → tty → console → guiw/terminal → shell。
- `user/guiw` 当前是 GUI 终端客户端，`src/guiw` 才是窗口服务；不能覆盖前者。
- 工作区已有用户改动：总览、syscall 头文件，以及 terminal → guiw 的客户端目录迁移，必须保留。

## syscall 迁移决策（设计阶段）

| 旧调用 | 目标 | 原因/限制 |
|---|---|---|
| 44–47 | 直接 IPC，增加 deadline、取消和显式 token | 内核基础机制 |
| 8、16、18 | 三个句柄入口；句柄操作复用受控子操作 | 不增加 syscall 编号 |
| 66–68 | 直接映射 | 页表隔离基础机制 |
| Yield、Sleep | 直接调度 | 当前线程操作 |
| fork/exec/线程/信号上下文操作 | 逐项保留必要直接机制 | 不得让代理服务的 current 替代客户上下文 |
| 消息队列/管道 | msgq/pipe 服务，Ring 3 可行性待验证 | 必须保持阻塞、EOF、继承和无新增 syscall |
| 设备/PCI/时间/系统信息 | 可信服务对象 | 端口访问不得进入 Ring 3 ABI |
| 共享内存等管理 | memory 服务 | 底层页框和页表留在内核 |
| TerminalId | Terminal 类和启动/继承状态 | 不保留独立 syscall |
| CreateEndpoint | 句柄入口的受控工厂操作待实现 | 必须能引导注册表，无循环依赖 |

## 实施状态

进行中：句柄共享引用及批量提交、RPC token、绝对截止时间、跨线程取消及等待节点清理。

后续：统一 Task/Thread/地址空间所有权、SMP 回收协议、Ring 0 直接接口、服务边界与对象化、syscall 收敛、guiw 与独立构建、服务恢复、运行验证和最终文档。

## 验证记录

- WSL Debian 的 g++、QEMU 可用。
- 初始 `make -j4 check` 因生成的 packages.mk 仍引用迁移前 `user/terminal/main.cc` 失败；需要修复生成依赖，不能将此视为代码基线通过。
- IPC/句柄基础改造后 `make -j4 kernel` 首次编译链接通过；这只是编译检查，尚不证明运行语义正确。
