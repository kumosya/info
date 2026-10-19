# 混合内核重构实施记录

规格：[hybrid-kernel-refactor-prompt.md](hybrid-kernel-refactor-prompt.md)。内存细节及最新证据见 [memory-progress.md](memory-progress.md)。

## P0–P4 核查

| 问题 | 处理 | 状态 |
|---|---|---|
| P0 非原子句柄传递、错误接收者假设 | 入队前授权/容量检查，队列持对象引用，实际接收 Task 批量安装；Reply move 先预留再提交 | 宿主事务/失败/权限/接收者测试通过 |
| P1 RPC 隐式线程关联 | 唯一 Reply Token，绑定 Endpoint、调用者、状态和服务进程；记录领取线程用于死亡通知 | 乱序/重复/过期回复测试通过 |
| P2 队列、超时、取消、丢失唤醒 | 8 槽 FIFO、独立 IRQ 合并、绝对 deadline、同进程取消、prepareWait 与持锁登记、关闭和对端死亡错误 | 宿主 IPC 与内联机制等待测试通过 |
| P3 Task/Thread/地址空间泄漏 | TaskReference、ThreadCleanup、HandleTable dispose、独立 TaskReclaimer 队列持引用；wait 摘除时原子交接回收责任 | 四核 32 轮清理请求者被杀测试通过；退出状态保留至 wait |
| P4 远端 CPU 仍使用旧地址空间 | quiesceTask 等待实际切换；exec 不可逆提交区禁止抢占但允许中断 | 四核 64 轮远端旧页表访问、exec 与并发 kill 测试通过 |

## 最新执行级别修正

- 按用户最新明确约束，所有服务进程运行于 Ring3，启动入口检查 CS；不再存在 Ring0 boot 服务进程。
- Ring0 保留 task.kernel、pci.kernel、device.kernel、memory.kernel、system.kernel 机制对象。
- pipe/msgq 的缓冲区、队列、通知、EOF 和等待请求已迁至 user/pipe、user/msgq；旧内核后端及未使用的内核 POSIX 文件表已移除。
- PS/2 控制器端口代码迁入 kernel/device/ps2.cc，PCI 配置由 pci.kernel 提供；OS 库已删除原始端口读写 API。
- 服务重注册时即时检查旧发布者死亡，修复周期 sweep 与新注册请求之间的时序窗口。
- 单核完整 Ring3 启动与管道/消息队列、服务死亡重注册、MM/信号运行测试通过：build/hybrid-ring3-smp1.log。四核最新复验也已通过：build/hybrid-ring3-smp4.log。

## 已落实的边界

- 删除 Ring 0 中断式 syscall，所有服务通过 Ring3 syscall/IPC 请求内核机制对象；内核内部使用具名类方法。动态磁盘 ELF 不得申请 ServiceRole/capabilities。
- syscall 仅面向 Ring 3，顺序为 IPC、三个句柄调用、映射、必要任务/调度/futex，共 19 个；无直接 I/O 端口操作。
- pipe/msgq 状态已迁入 Ring3 服务；memory.kernel/device.kernel/pci.kernel/system.kernel/task.kernel 是 Ring0 机制对象。驻留 task API 内联运行于认证调用线程，使用显式 Task/Thread，不冒充 CPU current。
- fork/exec 保留完整调用上下文入口；Process::getThreadId 与 signalReturn 已迁到 task 服务。
- init ServiceManager 运行 Ring 3，保护名按内核角色授权，查询仅发布 Write 端点，支持 generation、注销、死亡清理和重新注册；InitSupervisor 重启 boot 服务。
- 所有服务进程运行 Ring3，Ring0 仅保留 .kernel 机制对象。guiw 服务与 GUI 客户端后续迁到独立 D:/osinfo/guiw 项目，OS 仓库只安装包；user/Makefile 为其余用户空间的独立入口。见[桌面与开发环境记录](desktop-runtime-development-progress.md)。
- OS 非 syscall API 与全部服务业务函数使用类方法；审计包含 user/mm，白名单不再包含已迁移的旧自由函数。
- Terminal ID 属于 Terminal 类，无新增 syscall。
- 内存机制与策略、头文件归并和 PageObject 构造函数见独立内存记录。

## 文档和测试入口

- [混合内核架构图、RPC 状态、SMP 时序](../x86-64/architecture.md)
- [19 个 syscall 及历史迁移表](../x86-64/task/syscall.md)
- [内存管理](../x86-64/memory/memory.md)
- make check：构建、35 个可信 import、头文件边界和服务对象化审计。
- make -C user：独立服务/应用构建。
- script/check-kernel-ipc.sh、script/check-kernel-memory.sh。
- script/smoke-hybrid.py --memory-test：隔离镜像中的实际用户态内存与服务协议回归。

## 尚未宣称完成

设备死亡后的 DMA 停机与硬件重启协议仍需处理。服务重新注册不等于所有业务状态或硬件状态已经恢复。已通过的 96 轮确定性交错测试不能替代长期随机压力和设备故障注入。

## 生命周期补充验证

- build/hybrid-lifecycle-final-build.log：构建、35 个兼容导入、内存头文件及对象化审计通过。
- build/hybrid-lifecycle-result.log、build/hybrid-lifecycle-smp4-rerun.log：最新内核的 32 轮回收请求者被杀、64 轮跨 CPU exec/kill、完整 Ring3/MM 回归通过，检查进程退出码 0。
- exec 替换根时递增地址空间 generation；真实用户进程 exec 后验证新匿名页和 MM 元数据，标记为 exec resets memory policy generation passed。
- 修复 migrate 用瞬时队列成员状态判断成功的竞态：目标 CPU 已开始运行线程时，入队成功仍返回成功。
- 专项测试仅编入 build/tests/kernel-lifecycle.elf；生产内核不包含测试逻辑。

## 最终 Ring3 边界验证

- make check、make -C user：通过，见 build/hybrid-ring3-final-build.log 和 build/hybrid-ring3-user-build.log。
- 单核与四核 QEMU 运行测试：通过（脚本退出码 0），见 build/hybrid-ring3-smp1.log、build/hybrid-ring3-smp4.log。
- 运行用例包含匿名缺页、PROT_NONE 恢复、部分解除、COW、共享内存、文件委托、信号返回；新的 Ring3 pipe/msgq 数据、权限、优先级、EOF、超时，以及服务死亡、重新发布和重连也通过。
- 内核符号检查不再包含旧 Pipe、MessageQueue 或 POSIX FileTable 后端。
- 服务入口强制检查 CS=3；OS SDK 和服务代码没有原始端口访问 API。
- 内存管理头文件只允许 mm.h/page.h；PageObject 构造函数及失败/引用测试通过。
