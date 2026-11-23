# 物理内存：哪些页能用，怎样分配与归还

本文解释 `mm` 命名空间的物理页管理，覆盖 `src/kernel/memory/physical/physical.cc`、`buddy.cc`、`topology.cc` 的全部函数和相关内联接口。声明位于 `include/kernel/page.h`、`mm.h`。

物理内存可以看作真实仓库，启动位图是可用位置清单，PageFrame 是管理账本，buddy 空闲链是按大小分类的空箱目录。

## 1. 先理解页、块与地址

一页是 4096 字节。页编号等于物理页首地址除以 4096；`order` 表示一次申请 `2^order` 页，不是申请字节数。

| order | 页数 | 大小 |
| --- | --- | --- |
| 0 | 1 | 4 KiB |
| 1 | 2 | 8 KiB |
| 2 | 4 | 16 KiB |
| 8 | 256 | 1 MiB |

NUMA 将 CPU 和内存按访问远近分组。分配时优先找当前节点，有条件时再回退其他节点。物理分配返回物理地址，不能直接当普通指针使用；访问时需要内核映射。

## 2. 启动初始化

### 2.1 initPhy / PhyAlloc::init：先建立可分配页集合

`initPhy(boot)` 取得全局 `phyAlloc()`，再调用 `PhyAlloc::init(boot)`。`boot` 是引导阶段收集的内存表、模块、帧缓冲、页表根等信息。

当前 `PhyAlloc` 私有继承 `BuddyAlloc`：前者负责“哪些页可以使用”，后者负责“可用页如何拆分和归还”。`using` 公开所需的引用、查询和归属接口，基类启动接口仍由内部控制。

执行步骤：

1. **位图全部清零。** 每一位代表一个物理页，0 表示不可交给分配器。前 64 GiB 一共有 `2^24` 个 4 KiB 页，位图占 2 MiB。它是静态成员存储，不从堆申请。
2. **初始化 NUMA 拓扑。** 读取 SRAT，先确定各段物理内存属于哪个节点。
3. **第一遍读内存表，开放可用 RAM。** 只接受 `memoryAvailable` 描述，并裁剪到 64 GiB 上限；把完整落入区间的页置为 1。
4. **更新最高地址。** `highestAddress` 取被接受的可用描述的最高结束地址。它是地址跨度，包含其中的空洞，不是可分配字节总和。
5. **第二遍排除保留区。** 所有非 `memoryAvailable` 描述都清零。这样即使固件条目重叠，保留描述也始终优先，与条目顺序无关。
6. **排除实际占用。** 再排除低端 1 MiB、`bootstrapStart` 到 `bootstrapEnd` 的内核和启动布局、Multiboot 信息、所有模块和帧缓冲。帧缓冲长度用 `pitch × height` 计算，包含行尾填充。
7. **计算管理数组大小。** `count = highestAddress / 4096`；每个页编号都需要一条 `PageFrame`，包括保留页和空洞。所需元数据页数为 `ceil(count × sizeof(PageFrame) / 4096)`。
8. **寻找连续元数据空间。** 从页编号 256，即 1 MiB 开始扫描。可用则连续计数加一，遇到保留页就清零；找到第一段足够长的区域后记录起点。总空闲量足够但没有足够长的连续区间，也会失败。
9. **失败则停止。** 找不到元数据存储，或所需页数为零，输出 `insufficient page metadata storage`，调用 `cpu::halt()`。
10. **先保留账本自身。** 将元数据所在页从位图清除。否则分配器将来可能把自己的账本交给调用者覆盖。
11. **初始化 buddy。** 用 `directMap + metadata × 4096` 访问管理数组，调用 `BuddyAlloc::init`。启动汇编已建立前 64 GiB 的直接映射，所以这一步不依赖后面的 `initVir`。
12. **把可用页交给 buddy。** 再扫描位图，把“连续、全部可用、同一 NUMA 节点”的页归为一段，调用 `addRange(first, count, node)`。保留页和节点变化都会截断区间。
13. **输出结果。** 打印空闲页数和节点数，物理页分配接口开始可用。

`range(first, size, usable)` 是上述筛选的关键：开放时向内取整，只接受完整页；保留时向外取整，只要碰到某页就保留整页。例如 `[0x1800, 0x3800)` 开放时只开放 `[0x2000, 0x3000)`，保留时则覆盖 `[0x1000, 0x4000)`。

整个过程用“静态位图 → 预留管理数组 → 正式分配器”解决自举问题，没有用尚不存在的堆来创建物理分配器。

## 3. 物理页与 NUMA：完整函数表

### 3.1 PhyAlloc 和统一入口

位置：`physical/physical.cc`、`page.h`。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `phyAlloc()` | 返回全局物理分配器的引用，整个内核共用同一套页状态 |
| `initPhy(boot)` | 启动入口，转交 `PhyAlloc::init`，流程见本文启动初始化说明 |
| `PhyAlloc::init(boot)` | 筛选页、保留管理数组、初始化 buddy 并加入空闲区间 |
| `PhyAlloc::test(page)` | 输入页编号，返回启动位图中对应位是否为 1 |
| `PhyAlloc::set(page, usable)` | 设置一个启动位；不维护 buddy 空闲计数 |
| `PhyAlloc::range(first, size, usable)` | 输入物理字节范围，裁剪上限并按开放/保留规则取整，逐页设置位图 |
| `PhyAlloc::allocate(order, owner, node, strict, maximum)` | 将未指定节点换成当前 CPU 节点，再调用 `BuddyAlloc::allocate`；返回物理块首地址，失败为 0 |
| `PhyAlloc::retain/release/assignOwner/query` | 通过 `using` 公开的基类方法，语义见下一表 |
| `PhyAlloc::ownedBytes(owner)` | 所有者占用页数乘以 4096，返回字节数 |
| `PhyAlloc::totalBytes()` | 返回最高管理地址跨度，包含空洞 |
| `PhyAlloc::freeBytes()` | buddy 当前空闲页数乘以 4096 |
| `allocatePhysicalBlock(order, owner, node, strict, maximum)` | 统一块分配入口；默认 owner 为 0、节点自动选择、允许回退、无额外地址限制 |
| `allocatePhysicalPage()` | 相当于申请 0 阶块，即一页；不会自动清零 |
| `freePhysicalPage(address)` | 归还一页的一份引用；实际执行 0 阶释放并跳过 owner 匹配，返回是否通过检查 |
| `retainPhysicalBlock(address, owner)` | 增加指定物理块引用，返回是否成功 |
| `releasePhysicalBlock(address, order, owner)` | 按原始阶数归还一份引用，引用为零才真正释放 |
| `assignPhysicalPageOwner(address, owner)` | 给未归属块登记所有者，或确认现有归属 |
| `queryPhysicalPage(address, info)` | 查询地址所属块，将状态写入 `PhysicalPageInfo`；可输入块内地址 |
| `getOwnedMemoryBytes(owner)` | 返回按 owner 记账的已分配物理字节数 |
| `getTotalMemoryBytes()` | 返回管理地址跨度，不能当作可用 RAM 总和 |
| `getFreeMemoryBytes()` | 返回当前空闲物理字节数 |

### 3.2 BuddyAlloc

位置：`physical/buddy.cc`。空闲链表按 `heads_[node][order]` 组织，链中只存块头的页编号。`noFrame` 表示没有块头；物理地址 0 不作为成功分配结果。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `init(frames, count)` | 接收已准备好的元数据数组；清空链头和空闲数、初始化锁、原地构造各记录，所有页先为 `Reserved` |
| `mark(head, order, node, state)` | 重写整个块的记录：内部页为 `Interior` 并指向块头，块头记录阶数和指定状态 |
| `insert(head)` | 将空闲块头插入所属节点、所属阶数的双向链表 |
| `remove(head)` | 从空闲链摘除块头，修正前后链接 |
| `addRange(first, count, node)` | 输入页编号、页数、节点；确认全段仍为保留状态后，拆成对齐二次幂块入链；失败返回 false |
| `allocate(order, owner, preferred, strict, limit)` | 按节点和阶数找块，必要时拆分；`limit` 是请求块结束物理地址的上限；返回首地址或 0 |
| `retain(address, owner)` | 仅接受已分配块头，核对 owner 和引用上限后加一；失败 false |
| `release(address, order, owner)` | 校验块头、阶数、owner；减引用，归零后尝试伙伴合并；失败 false |
| `assignOwner(address, owner)` | 只接受已分配块头；允许从 owner=0 设置归属，或保持同一 owner，拒绝直接抢走其他 owner 的块 |
| `query(address, info)` | 从块内地址找到块头并返回块状态，不增加引用 |
| `pagesOwned(owner)` | 扫描已分配块头，按 `2^order` 累计页数，不重复计算内部页 |
| `freePages()` | 在锁内读取当前空闲页数 |

`retain/release` 中 `owner=UINT64_MAX` 表示可信内核调用可跳过 owner 匹配，块状态、阶数等检查仍然有效。`mark/insert/remove` 是内部函数，由初始化阶段或已持锁的路径调用。

### 3.3 伙伴分配和释放流程

`addRange` 每次选“地址满足对齐、长度不超过剩余范围”的最大块。例如页编号 `[256,269)` 共 13 页，会拆为 8 页、4 页、1 页三块。

`allocate` 的执行顺序：

1. 检查分配器已初始化、阶数和节点合法。
2. 持锁，优先找指定节点；严格模式不换节点，普通模式按节点编号顺序回退。
3. 从请求阶数向上搜索，遍历空闲链，跳过超过物理地址上限的候选。
4. 摘下找到的块。若块太大，反复对半拆开，继续使用低地址半块，将高地址半块放回对应空闲链。
5. 将最终块标为 `Allocated`，记录 owner，初始引用设为 1。
6. 空闲页数只减去实际交出的页数，返回物理地址。

例如申请一页却只找到四页块，就先拆成两个两页块，再把低半块拆成两页单页块，最后交出其中一页。

`release` 的执行顺序：

1. 核对输入是合法已分配块头，阶数与最初分配一致，owner 符合要求。
2. 引用减一。仍大于零时成功返回，物理内存继续保留。
3. 引用为零时恢复空闲计数，用 `head XOR (1 << order)` 找到同阶伙伴。
4. 伙伴必须在范围内、处于 `Free`、阶数相同且属于同一节点，才能摘链合并。
5. 合并使用更低页编号作为新块头，阶数加一，继续向上尝试。
6. 最后统一重写整个块的记录并入链。

所以两个相邻空闲区域不一定能合并：还必须满足伙伴对齐、同阶数、同节点等条件。

### 3.4 NUMA 拓扑函数

位置：`physical/topology.cc`、`mm.h`。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `readSratValue(data, bytes)` | 按小端顺序逐字节拼成整数，避免未对齐读取 |
| `memoryTopology()` | 返回全局拓扑对象 |
| `MemoryTopology::init(boot)` | 找到 ACPI SRAT，按条目长度遍历；读取启用的内存、旧 APIC、x2APIC 亲和信息；缺表时保留默认节点 0 |
| `nodeForDomain(domain)` | 将固件域编号转换为紧凑节点编号；新域占一个槽，满时返回 `anyMemoryNode` |
| `nodeForAddress(address)` | 查物理地址所属区间，未找到返回节点 0 |
| `enableCpuLocal()` | 表示 CPU 局部数据已就绪，允许查询真实当前 CPU |
| `currentNode()` | 未就绪返回 0；就绪后根据当前 APIC ID 查询节点，未匹配也返回 0 |
| `nodeCount()` | 返回已建立的节点数 |
| `Range()`、`Range(start, stop, domain)` | 默认构造区间，或保存起止地址和节点 |
| `Range::contains(address)` | 判断地址是否属于左闭右开区间 |
| `Cpu()`、`Cpu(id, domain)` | 默认构造 CPU 记录，或保存 APIC ID 与节点 |
| `Cpu::matches(id)` | 比较记录的 APIC ID |

当前最多记录 16 个节点、64 段内存亲和区间、256 个 CPU 亲和记录。分配回退按节点编号进行，没有根据 SLIT 距离排序。

### 3.5 页状态内联函数

`FrameState` 的四种状态：`Reserved` 为不可分配页，`Free` 为独立空闲块头，`Allocated` 为已分配块头，`Interior` 为块内部页。内部页通过 `head` 找到整块信息。

| 函数 | 作用 |
| --- | --- |
| `PageFrame::reset()` | 恢复默认管理记录，不清零物理页内容 |
| `PhysicalPageInfo()` | 默认构造查询结果 |
| `PhysicalPageInfo(owner, references, node, order, state)` | 用指定字段构造查询结果 |
| `getOwner()/getReferences()/getNode()/getOrder()/getState()` | 分别读取所有者、物理块引用数、节点、阶数、状态 |

## 4. 用例子理解引用、回收和失败

### 4.1 两个使用者怎样归还同一块

申请 2 阶块返回 P，owner=42，初始引用为 1，空闲页数减少 4。新使用者调用 `retainPhysicalBlock(P, 42)` 后引用为 2；第一次 `releasePhysicalBlock(P, 2, 42)` 只减到 1，最后一次才让计数归零，恢复 4 页并尝试合并。

retain/release 必须传块头，不能释放 P+4096，也不能用 0 阶归还 2 阶块。query 可以输入块内地址，因为它会先找到块头。

### 4.2 MemoryTopology::init 怎样读固件

1. 恢复默认节点 0，关闭 CPU 局部查询。
2. 通过 ACPI 找 SRAT，缺表或表太短就使用默认配置。
3. 遍历条目，每次先检查长度，再读取启用的内存亲和、旧 APIC 或 x2APIC 信息。
4. 将固件 domain 编号转换为紧凑节点编号，分别登记内存区间和 CPU 对应关系。
5. 内存区间要求非空且加法不溢出，记录容量不够就不再接收对应记录。
6. 后续地址或 CPU 查询未匹配时回退节点 0。

内核节点编号用于数组索引，不必等于固件的原始 domain 编号。

### 4.3 有空闲内存为什么还会失败

| 现象 | 应检查什么 |
| --- | --- |
| 元数据存储不足 | 是否存在足够长的连续可用区间，而不仅是空闲总量 |
| 大块申请失败 | 空闲页可能分散，不能组成满足对齐的连续块 |
| strict 模式失败 | 首选节点没有合适块，即使其他节点有也不回退 |
| maximum 限制后失败 | maximum 是请求块结束物理地址上限，不是请求大小 |
| release 返回 false | 块头、阶数、状态、引用或 owner 是否不匹配 |
| total 大于 free | total 包含地址空洞、保留区域和已使用空间 |

底层物理页申请不自动清零，调用方需要时应显式清零。

## 5. 继续阅读

整体依赖见 [内存总览](memory.md)，页面访问和对象包装见 [对象与内核堆](object.md)，页表关系见 [虚拟内存](virtual.md)。
