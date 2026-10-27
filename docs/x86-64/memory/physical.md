# 物理内存与 NUMA 分配

声明位于 `include/kernel/page.h` 和 `mm.h`，命名空间为 `kernel::mm`。实现分为 `physical/physical.cc`、`buddy.cc`、`topology.cc`，分别负责引导筛选、实际页块管理和 NUMA 拓扑。

## PhyAlloc 的引导过程

PhyAlloc 保存启动可用页位图、最高管理地址和 BuddyAlloc。位图用于建立初始可分配集合，不是日常分配时反复线性扫描的空闲页表。

`init(boot)` 读取 MemoryRegion，先加入可用 RAM，再减去固件保留范围。保留描述优先于重叠的可用描述；可用区间向内取整，保留区间向外取整，避免半页被误分配。

低端 1 MiB、内核及引导页表/栈、Multiboot 信息、启动模块和帧缓冲全部排除。随后从剩余连续 RAM 中保留 PageFrame 数组，再将连续且同 NUMA 节点的可用区间交给 BuddyAlloc。必须先排除元数据自身，否则分配器可能把保存页状态的内存重新交给调用者。

| 函数 | 功能与参数 |
| --- | --- |
| `range(first, size, usable)` | first、size 是物理字节范围；按可用或保留规则修改初始位图，并裁剪支持范围。 |
| `test(page)`、`set(page, usable)` | 读取或设置物理页编号对应的启动位。 |
| `allocate(order, owner, node, strict, maximum)` | 申请连续页块；未指定节点时优先当前 CPU 节点，strict 控制是否允许回退，maximum 是物理结束地址上界。 |
| `initPhy(boot)` | 引导初始化入口。 |
| `allocatePhysicalBlock(order, owner, node, strict, maximum)` | 返回物理地址，失败为 0；块大小为 `4096 × 2^order`。 |
| `allocatePhysicalPage()`、`freePhysicalPage(address)` | 单页便捷入口，释放仍经过块状态和阶数检查。 |

totalBytes 返回最高管理地址表示的跨度，包含其中的保留洞，不能直接当作所有可分配 RAM 之和。freeBytes 来自 buddy 当前空闲页数，ownedBytes 按已分配块所有者统计。

## PageFrame 和 BuddyAlloc

PageFrame 记录块头、节点、阶数、状态、所有者、引用和空闲链链接。块内页指回同一块头；只有块头代表独立分配。FrameState 区分 Reserved、Free、Allocated 和 Interior。

BuddyAlloc 保存帧数组、按节点与阶数组织的空闲头、空闲页总数和锁。辅助函数 mark、insert、remove 要求在初始化阶段或相应分配器锁保护下执行。

| 成员函数 | 功能与参数 |
| --- | --- |
| `init(frames, count)` | 接管调用者准备的元数据数组，初始所有帧保留，不自动加入空闲集合。 |
| `addRange(first, count, node)` | 接受仍为 Reserved 的页编号范围，拆成对齐且能容纳的最大阶块。 |
| `allocate(order, owner, preferred, strict, limit)` | 先按节点选择，再从请求阶数向上寻找块，分裂后返回指定阶数。 |
| `retain(address, owner)` | 为已分配块头增加引用；拒绝内部页、错误所有者和计数溢出。 |
| `release(address, order, owner)` | 减少匹配块引用，归零后合并伙伴并重新入空闲链。 |
| `assignOwner(address, owner)` | 为未归属块赋所有者或确认已有所有者，不任意转移别人正在使用的页。 |
| `query(address, info)` | 允许输入块内物理地址，返回其块头状态。 |
| `pagesOwned(owner)`、`freePages()` | 所有者页数和当前空闲页数。 |

owner 为 UINT64_MAX 时，可信释放/引用路径可以跳过所有者匹配，但不能跳过块状态和阶数要求。

## 分裂和合并为什么这样执行

申请时找到的块可能大于请求。分裂保留低半块继续处理，将高半块挂回较小阶空闲链，直到得到请求大小。分裂不消耗 RAM，因此只在最终分配时扣除实际请求页数，块头建立初始引用 1。

释放不是立即合并：仍有引用时只减计数。归零后，以 `head XOR (1 << order)` 计算同阶伙伴。伙伴必须在管理范围内、为 Free 块头、阶数相同且属于同一 NUMA 节点。任一条件不满足便停止，防止跨保留洞、跨活跃分配或跨节点拼成虚假的连续块。

合并先从空闲链摘掉伙伴，再以较低页号代表新块，向更高阶继续。最后统一 mark 并插入新块，避免旧块头和新块头同时出现在链中。

## NUMA 拓扑

MemoryTopology 保存固件 proximity domain 到稠密节点编号的对应、物理区间和 APIC 对应节点。`init(boot)` 经 ACPI Tables 读取 SRAT；按条目长度逐字节取字段，避免未对齐访问，缺失或无法记录时回退节点 0。

`nodeForAddress(address)` 选择物理地址所属节点；`currentNode()` 根据当前 APIC ID 选择优先节点；`enableCpuLocal()` 在 GS 数据就绪后才允许读取当前 CPU；`nodeCount()` 返回已建立节点数量。

当前实现按首选节点及回退顺序分配，不做 SLIT 距离排序。SRAT 只影响放置偏好，不能改变物理范围保留和所有权规则。
