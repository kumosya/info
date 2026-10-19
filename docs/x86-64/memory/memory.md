# 内存管理

内存管理只公开 mm.h 和 page.h 两种头文件名：内核位于 include/kernel/，MM 内部位于 user/mm/，OS 客户端位于 include/os-c++/os/mm.h。实现继续按 physical、object、virtual、service 子目录拆分。

## Ring 0 机制

| 路径 | 职责 |
|---|---|
| physical/buddy.cc | 按阶连续物理页块，分裂/合并、引用、owner、node 与地址上限 |
| physical/topology.cc | ACPI SRAT 内存域及 CPU 域，首选 NUMA 节点和回退 |
| physical.cc | 引导可用内存、PageFrame 元数据保留及物理分配入口 |
| object/slab.cc | 固定大小 ObjectCache，空 Slab 回收 |
| object/per_cpu.cc | 每 CPU magazine，减少共享缓存争用 |
| object/pool.cc | 外部存储 FixedPool，容量与重复释放检查 |
| object/mapping.cc | 高地址 NX 内核堆别名与清零 |
| object/page_object.cc | PageObject 构造、有效性、引用及最终物理块释放 |
| virtual.cc | 四级页表、保护、地址翻译、地址空间复制与销毁 |
| virtual/page_mappings.cc | 页面对象映射引用、拆分、fork 登记和 COW 写权限限制 |
| virtual/operations.cc | 显式 Task 参数的 map/unmap/protect 机制 |
| shared.cc | 命名共享对象、映射引用及最后释放 |
| service/service.cc | memory 驻留服务与跨任务授权 |
| service/pager.cc | Ring 3 pager 绑定、缺页 RPC、fork 元数据交接及缓冲区 fault-in |

Buddy 使用实际 RAM 对应的 PageFrame 元数据，不再线性扫描位图。小块堆走 Slab/Per-CPU Cache，大块堆走 Buddy；FixedPool 用于有明确容量的映射记录。NUMA 最多 16 节点，当前按 SRAT 首选与回退分配，未实现 SLIT 距离排序。

PageObject(order, owner, node) 直接构造并清零物理块。调用者先分配对象存储，再 placement-new；必须检查 valid()。order 超限或分配失败得到无效对象，release() 仍能安全释放元数据。最后一个对象引用消失才析构并释放物理块；没有静态 create 工厂。

memory 服务提供页对象、连续页块、共享对象、usage 和 cache reclaim 接口。跨任务操作要求固定 boot MM 身份拥有的 Memory 能力；客户端传入 PID 不能自行获得权限。基本映射保留 syscall 8–10。内核内部直接使用具名方法。

用户地址范围为 [0x10000, 2^47)，页大小 4096。内核直接映射从 0xffff800000000000 开始，NX 堆别名从 0xffff804000000000 开始。所有用户写入与执行权限遵守 W^X。

解除映射先预留共享对象和 PageObject 的拆分记录，再撤销 PTE、完成 TLB shootdown，最后释放映射引用；元数据耗尽不会留下半拆分记录。修改页权限保留硬件 Accessed/Dirty 位。

## Ring 3 MM 策略

MM 是固定 boot 模块，注册名 mm；驻留机制服务名称为 memory.kernel，二者职责不同。

| 对象 | 职责 |
|---|---|
| RegionTable | gap 查找，重叠/溢出检查，插入、移除、分裂、合并、权限上限 |
| MemoryState | 进程代际、VMA、resident 页与 backing 引用；退出和 exec 清理 |
| PageAllocator | 按进程配额请求零页，失败时请求 reclaim 后重试 |
| Pager | 匿名零填充、共享映射、文件偏移读取、重复缺页检查及 COW |
| ReclaimManager | cache 回收、Accessed/Dirty 分类、跳过 pinned/I/O/COW/私有脏页 |
| SharedMemoryManager | 句柄权限校验、共享 backing 引用和映射最大权限 |
| MemoryManager | IPC 协议及 VMA/PTE 事务协调 |

RegionTable 容量 512，resident 记录 8192，backing 记录 256，domain 记录 128。容量耗尽明确返回错误。MM 单线程串行处理请求，重复缺页检查已有 PTE；不会重复分配同一已就绪页面。

内核把用户缺页转为认证 senderPid 的 RPC，截止时间为 5 秒；MM 回复成功后再次校验实际 PTE。MM 自身缺页禁止递归。syscall 的用户缓冲区先 fault-in，再持地址空间锁校验并复制。

匿名页延迟申请清零 PageObject。文件映射使用已打开描述符的 FS 委托：FS 保留原 OpenFileDescription，记录 issuer 和一次性 Token；只有可信 MM 能按原 senderPid 兑换。MM 不以自身权限重新按路径打开文件。关闭原 fd 后 backing 仍持引用。

fork 在子线程可运行前复制 MM VMA 与 resident 引用；私有页面在父子两侧清 Write，写缺页复制到新对象后替换映射。普通非 external 页仍深拷贝，共享对象保持共享。内核映射记录阻止直接 ProtectMemory 绕过 COW。

PROT_NONE 撤销 PTE 并保留 backing；恢复权限后缺页重建。munmap/mprotect 在暂存 RegionTable 上完成预检，失败不提交元数据；PTE 修改失败时尝试恢复先前状态。

libc 的 mmap/munmap/mprotect/msync 调用 os::VirtualMemory，区域状态由 MM 统一管理。支持私有匿名、私有文件和共享内存对象映射，以及部分区域保护/解除。MAP_FIXED、文件 MAP_SHARED、匿名 MAP_SHARED 当前明确返回 ENOTSUP；私有 msync 和共享内存同步不写回源文件。没有 Swap Manager，不会丢弃不可重建的私有脏页；内存不足明确失败。

服务重启不自动恢复旧 VMA。客户端收到端点失败后清缓存，下次重连；缺失 VMA 的旧请求明确失败，非法缺页终止用户线程。进程退出或 exec 后，MM 按 Task 状态及地址空间 generation 回收旧记录。

## 验证

- script/check-kernel-memory.sh：Buddy、所有权与引用、NUMA、Slab、Pool、Per-CPU、PageObject 构造/失败与 VMA 原子性。
- tests/mm-runtime.cc：用户态延迟分页、保护、部分解除、COW、共享对象、文件委托和信号返回。
- script/smoke-hybrid.py --memory-test：用隔离磁盘运行上述测试，再进入真实 GUI 终端。
- SMP exec/kill 压力和设备 DMA 停止协议另见[架构文档](../architecture.md)，不能用启动成功代替压力验证。
