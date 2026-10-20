# memory.md 实施记录

> 2026-10-19 后续更正：服务目录、映射入口及当前验证见 [v0.1.1 实施记录](v0.1.1-refinement-progress.md)。以下保留此前实现阶段的记录，不能用旧路径或旧测试日志代替当前状态。

规格见 [memory.md](memory.md)。本文件记录实际状态，不替代源码与测试证据。

## 最新边界

所有服务进程运行于 Ring3；只有 .kernel 机制对象在 Ring0。pipe/msgq 状态也已迁到 Ring3，旧内核后端和未使用的内核 POSIX 文件表已删除。OS 库没有原始端口 API；PCI/PS2 端口操作位于内核。

## 已实现

- GetThreadId、SignalReturn 迁到驻留 TaskService，syscall 当前为 19 个，编号和历史迁移表已同步。
- 内核 Buddy、连续阶数页块、owner/引用、SRAT NUMA 首选与回退、Slab、Per-CPU Cache、FixedPool 和 NX 堆别名。
- PageObject 使用构造函数，失败可检查 valid；删除 create 工厂。宿主测试覆盖清零、非法阶数、内存耗尽与引用释放。
- 内存头文件仅使用 mm.h/page.h；实现按 physical/object/virtual/service 拆分。check-memory-headers.py 已加入 make check。
- Ring 0 memory 服务提供页对象、共享对象、统计、回收和授权跨进程映射；低层 map/unmap/protect 仍为保留机制。
- Ring 3 MM 已作为固定 boot 服务启动，包含 RegionTable、MemoryState、PageAllocator、Pager、ReclaimManager、SharedMemoryManager。
- VMA gap、重叠和溢出检查、拆分/合并、最大权限、部分 munmap/mprotect、PROT_NONE 恢复。
- 匿名延迟零填充、共享对象、私有文件映射；文件由 FS 兑换绑定 issuer 的一次性描述符委托，原 fd 关闭后 backing 保留引用。
- fork 在子线程运行前复制 MM 元数据；私有页面在父子两侧写保护，写缺页复制对象；内核映射记录阻止直接 ProtectMemory 绕过 COW。
- 保留硬件 Accessed/Dirty 位，记录 PROT_NONE 与 COW 前的私有脏状态；干净文件页可以回收，pinned/I/O/COW/脏页不直接丢弃。
- libc mmap/munmap/mprotect/msync 和 shm_open/shm_unlink 连接 MM；syscall 输出缓冲区支持先 fault-in。
- 修复向自身发信号把正在完成的 task 请求错误打断的问题；Endpoint 嵌套操作引用由 ThreadCleanup 管理。
- Endpoint 工厂与取消改为 os::Endpoint 类方法；OS 自由函数审计白名单收紧到当前 syscall 包装。
- 已同步 README、内存、ABI、服务、OS 库文档，并加入含架构图、RPC 状态图和 SMP 回收时序的 architecture.md。

## 验证证据

- build/hybrid-mm-host-memory.log：实际 Buddy、对象缓存、PageObject 及 RegionTable 宿主测试通过。
- build/hybrid-mm-host-ipc.log：实际句柄/Endpoint/驻留 Pipe 的事务、超时、取消、令牌与关闭测试通过。
- build/hybrid-mm-audit.log：服务与 OS 对象化审计通过。
- build/hybrid-smoke.6sr0rryt/serial.log：4 CPU 实际匿名、保护、部分解除、COW、共享、文件与信号测试通过，六个 shell 启动；该次脚本因跨 CPU 串口日志互相穿插而误判失败，已修复整段串口输出互斥。
- build/hybrid-ring3-smp1.log：所有服务经 Ring3 入口启动，新 pipe/msgq、服务死亡后重注册、MM 和信号用例通过；四核最新复验也已通过：build/hybrid-ring3-smp4.log。

## 当前明确边界

- MAP_FIXED、文件 MAP_SHARED、匿名 MAP_SHARED 返回 ENOTSUP；没有 Swap Manager，不丢弃无法重建的私有脏页。
- NUMA 尚无 SLIT 距离排序。
- MM 重启后不自动恢复旧 VMA，旧映射操作或缺页明确失败；不会伪装成已恢复状态。
- Task 清理由专用 TaskReclaimer 持引用完成，已通过 32 轮请求者被杀及 64 轮跨 CPU exec/kill 交错测试；证据见 build/hybrid-lifecycle-result.log。设备 DMA 停止与硬件安全重启、长期随机压力仍未验证。
- 地址空间根替换递增 generation，MM 据此丢弃旧映像元数据；新增真实 exec 后匿名缺页/写入/解除映射回归已通过。

## 最终 Ring3 边界验证

- make check、make -C user：通过，见 build/hybrid-ring3-final-build.log 和 build/hybrid-ring3-user-build.log。
- 单核与四核 QEMU 运行测试：通过（脚本退出码 0），见 build/hybrid-ring3-smp1.log、build/hybrid-ring3-smp4.log。
- 运行用例包含匿名缺页、PROT_NONE 恢复、部分解除、COW、共享内存、文件委托、信号返回；新的 Ring3 pipe/msgq 数据、权限、优先级、EOF、超时，以及服务死亡、重新发布和重连也通过。
- 内核符号检查不再包含旧 Pipe、MessageQueue 或 POSIX FileTable 后端。
- 服务入口强制检查 CS=3；OS SDK 和服务代码没有原始端口访问 API。
- 内存管理头文件只允许 mm.h/page.h；PageObject 构造函数及失败/引用测试通过。
