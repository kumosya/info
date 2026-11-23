# 虚拟内存：页表、映射、锁页与地址空间

本文覆盖 `src/kernel/memory/virtual/` 中 `virtual.cc`、`operations.cc`、`page_mappings.cc`、`locked.cc` 的全部函数。页表接口属于 `mm`，锁页区间属于 `task::AddressSpace::Residency`。声明主要位于 `include/kernel/page.h` 和 `task.h`。

## 1. 分清页表、对象记录和 VMA

| 信息 | 回答什么问题 | 维护者 |
| --- | --- | --- |
| 页表 | 某个虚拟页现在指向哪一页，CPU 能否访问 | 内核页表函数 |
| Maps / SharedMemory 记录 | 使用哪个对象、谁保留引用、最高授权是什么 | 内核登记表 |
| VMA | 哪段地址逻辑上有效、来自什么文件、缺页怎样处理 | 用户态 MM |

虚拟地址像进程自己的门牌号，物理地址是真实房间；相同门牌在不同进程可以指向不同房间。root 是顶层页表的物理地址，CR3 保存当前 CPU 使用的根，TLB 缓存最近的翻译。

用户范围映射要求页对齐，范围是 `[0x10000, 0x0000800000000000)`，单次最多 64 MiB。protection=0 为只读 NX，1 为可写 NX，2 为只读可执行，3 会被拒绝。底层单页接口的检查与上层范围接口有差别。

## 2. 接管启动页表

### 2.1 initVir：接管启动页表

1. 检查 `boot` 和启动页表根，缺失则输出错误并停机。
2. 保存根物理地址到 `kernelAddressSpaceRoot`。
3. 清除顶层页表第 0 项，移除启动时的低地址恒等映射。
4. 重新加载 CR3，让 CPU 使用更新后的页表。
5. 保留高半区直接映射，后续以高地址访问物理内存。

它接管已经建立的页表，而不是从完全没有分页的状态开始建立分页。

## 3. 页表：单页映射、翻译和跨空间复制

### 3.1 四级页表如何定位一页

每张页表一页，包含 512 个 64 位表项。普通 4 KiB 页的虚拟地址拆成：

```text
高位          39       30       21       12        0
              | PML4 | PDPT  | PD    | PT    | 页内偏移 |
                 9位     9位     9位     9位      12位
```

`mapPageInAddressSpace(root, virtual, physical, flags)` 的流程：

1. 检查两个地址都按 4 KiB 对齐。
2. 取得全局页表锁，调用 `mapPageLocked`。
3. 从虚拟地址取出四级索引，查询根页 owner。
4. 沿各级查找，缺少中间页表时分配并清零，再写入父表。用户位需要沿上层传播。
5. 遇到 1 GiB 大页路径拒绝；遇到 2 MiB 大页可先拆成 512 个 4 KiB 映射。
6. 若目标叶子已存在则拒绝覆盖；否则写入物理地址、权限和存在位。
7. 对非 external 用户页，按根 owner 记账。
8. 本地执行 `invlpg`，释放锁并返回结果。

注意：拆大页保留原映射关系，并不自动将目标叶子变空；已有映射仍不会被新映射覆盖。建表失败也可能留下已经准备好的中间表，由地址空间后续统一清理。

### 3.2 页表内部辅助函数

位置：`virtual/virtual.cc`。名字带 `Locked` 表示调用者应已持有页表锁。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `getTable(address)` | 从物理地址或表项去掉标志，添加直接映射基址，返回页表指针 |
| `getTablePhysicalAddress(table)` | 将直接映射中的页表指针减去基址，还原物理地址 |
| `allocateTable()` | 申请物理页并清零 512 项，返回表指针或 nullptr |
| `getOrCreateTable(parent, index, flags, owner)` | 缺失时创建下级表并登记 owner，根据 flags 传播用户位，返回下级表 |
| `splitLargePage(directory, index)` | 将 2 MiB 叶子展开为 512 个 4 KiB 叶子，先准备完整新表再替换父项；失败 nullptr |
| `mapPageLocked(root, virtual, physical, flags)` | 安装一个不存在的叶子，创建必要中间表；成功 true |
| `unmapPageLocked(root, virtual)` | 查找并清除已有普通页叶子，不释放后备页、不新建表 |
| `protectPageLocked(root, virtual, flags)` | 修改普通页权限，保留物理地址、external、locked、accessed/dirty |
| `translateLocked(root, virtual, flags)` | 逐级翻译，支持 1 GiB、2 MiB、4 KiB 叶子；返回包含页内偏移的物理字节地址，失败 0，可输出叶子标志 |
| `editPages(root, address, size, flags, protect)` | 一次最多 64 页；先校验整批，再统一解除或改权限，最后进行批量 TLB 失效 |
| `releaseUserPageTables(table, level)` | 递归释放私有用户页表，普通叶子归还物理页，external 叶子跳过 |
| `findDumpPage(table, level, base, cursor, address, flags)` | 递归找游标后的驻留用户小页；跳过大页和设备缓存映射，找到返回 true |

### 3.3 页表对外函数

| 函数 | 输入、作用和结果 |
| --- | --- |
| `initVir(boot)` | 接管启动页表并移除低地址恒等映射，初始化流程见 [总览](memory.md) |
| `mapPage(virtual, physical, flags)` | 使用内核根调用单页映射 |
| `mapPageInAddressSpace(root, virtual, physical, flags)` | 在指定根中映射一页，成功 true |
| `unmapPage(virtual)` | 在内核根中解除一页映射 |
| `unmapPageInAddressSpace(root, virtual)` | 清 PTE，失效本地和远端旧翻译；不释放物理页 |
| `protectPageInAddressSpace(root, virtual, flags)` | 修改一页用户权限，并使相关 CPU 旧翻译失效 |
| `unmapPagesInAddressSpace(root, address, size)` | 调用 editPages 批量解除，一次最多 64 页，不管理对象引用 |
| `protectPagesInAddressSpace(root, address, size, flags)` | 调用 editPages 批量改用户权限，一次最多 64 页 |
| `getPhysicalAddress(virtual)` | 在内核根查询物理地址，并非查询当前进程根 |
| `getPhysicalAddressInAddressSpace(root, virtual, flags)` | 持页表锁翻译指定根，可输出叶子标志 |
| `validateUserMemory(root, address, size, writable)` | 检查整个范围每页存在且用户可访问，需要时检查可写；零长度成功，不主动补页 |
| `allocateUserPageInAddressSpace(root, address, flags)` | 申请并清零一页，再尝试映射；失败归还新页，返回 `UserPageMapResult` |
| `copyToAddressSpace(root, destination, source, size)` | 从内核源缓冲向目标根写入，逐页翻译后经直接映射复制 |
| `copyFromAddressSpace(destination, root, source, size)` | 从指定根读取到内核缓冲，逐页翻译复制 |
| `nextDumpPage(root, cursor, address, flags)` | 持页表锁枚举下一页可转储用户小页；不触发缺页 |
| `getKernelAddressSpaceRoot()` | 返回内核页表根物理地址 |
| `createAddressSpaceRoot()` | 新建根，低半区清空，高半区复制内核公共根项，返回物理地址或 0 |
| `cloneAddressSpaceRoot(source, destination)` | 将用户普通页深拷贝到已有目标根，external 页仅复制映射；失败 false |
| `destroyAddressSpaceRoot(root)` | 释放用户低半区和根，拒绝零地址与内核根 |
| `switchAddressSpace(root)` | 只在当前 CPU 切换 CR3；0 表示内核根，相同根不重复切换 |

`UserPageMapResult` 区分 `Success/InvalidArgument/OutOfMemory/MappingFailed`。`allocateUserPageInAddressSpace` 的底层地址下限为 4096，范围映射接口的下限为 0x10000，不能把两层检查混为一谈。

`validateUserMemory` 对当前系统服务自己的高半区地址有专门例外：根必须确实属于当前系统服务，此时不要求 pageUser，但仍检查映射存在和需要的写权限。普通用户地址不能使用这个例外。

`copyTo/From` 不切换 CR3，按每页剩余长度复制，下一页重新翻译；失败前可能已复制一部分。`copyTo` 不负责替调用者实施用户写权限或 COW 策略。

## 4. 用户范围操作与页对象映射登记

硬件页表只记录“地址指向哪里”，还需要 `Maps` 记录“谁持有对象、允许哪些权限、解除时释放哪份引用”。

### 4.1 范围操作完整函数表

位置：`virtual/operations.cc`。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `error(value)` | 将负系统错误码转换为 uint64_t 表示，仅供本文件使用 |
| `validUserRange(address, size)` | 检查非空、无溢出且在用户范围；页对齐由外层函数另行检查 |
| `mapUserPages(target, address, size, protection)` | 立即为匿名范围申请普通页；地址和大小需页对齐，单次最多 64 MiB；成功返回 address，失败负错误码 |
| `unmapUserPages(target, address, size, preserveLocks)` | 整段预检、预留记录、撤销映射和归还引用；成功 0；preserveLocks 保留锁页约定供内部替换页使用 |
| `protectUserPages(target, address, size, protection)` | 检查对象权限上限和全部页面后分批修改权限；成功 0，失败负错误码 |

### 4.2 匿名 mapUserPages 流程

1. 检查目标不是 idle、地址大小页对齐、非空、用户范围、64 MiB 上限和权限组合。
2. 锁定目标地址空间，确认根仍存在。
3. 复制锁页区间记录；启用 future 锁页时，先在副本中加入新区间并检查限额。
4. 转换 protection 为用户、写、NX 标志。
5. 每次申请一个物理页，安装映射，然后经直接映射将这一页清零；按锁页记录设置 pageLocked。
6. 任意一步失败，归还当前新页，再倒序撤销本次已映射的前缀，完成失效后释放对应页，不碰原有映射。
7. 全部成功才提交锁页记录，返回虚拟起点。

### 4.3 Maps 和 Map 完整函数表

位置：`virtual/page_mappings.cc`、`page.h`。记录存储有 2048 个预置 `Map` 槽，由 FixedPool 管理。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `maps()` | 返回全局页对象映射登记表 |
| `Map()` | 默认构造空记录 |
| `Map(target, object, address, size, offset, protection, next)` | 保存目标任务、页对象、虚拟范围、对象偏移、权限上限和链指针 |
| `Map::overlaps(first, length)` | 判断两个左闭右开区间是否相交 |
| `Maps::init()` | 初始化锁、空链和固定记录池，返回成功与否 |
| `Maps::allocate()` | 从池取槽并原地构造空 Map，耗尽 nullptr |
| `Maps::map(target, object, address, size, offset, protection, maximumProtection)` | 校验并逐页映射页对象，成功发布记录并 retain，失败 false |
| `Maps::prepareUnmap(target, address, size, reservation)` | 如果删中段会产生左右两条记录，预留额外槽，通过 reservation 输出；不改页表 |
| `Maps::completeUnmap(target, address, size, reservation)` | PTE 撤销后删除、缩短或拆分记录；同步对象偏移、引用并归还剩余预留槽 |
| `Maps::releaseTask(target)` | 删除任务所有页对象记录并 release；不代替页表销毁 |
| `Maps::cloneTask(source, target)` | 先确认槽数充足，再复制源记录并为每条新记录 retain |
| `Maps::protectionAllowed(target, address, size, protection)` | 检查所有重叠记录，拒绝越过最大权限或给 COW 记录直接加写权限 |
| `Maps::markCopyOnWrite(target, address, size)` | 要求精确匹配记录，逐页去掉写权限并失效 TLB，成功后设置 cow |
| `Maps::nextManagedMapping(target, cursor, address, size)` | 找起点不小于 cursor 的下一段 pagerManaged 记录，供服务恢复 |
| `Maps::dumpAllowed(target, address)` | 判断地址是否属于可信内存服务建立的页对象映射，供核心转储筛选 |

`Maps::map` 的流程是：检查对象范围和权限 → 锁目标空间和登记表 → 拒绝记录重叠 → 先取记录槽 → 试算锁页 → 逐页建立 external PTE → 成功后 retain 并挂链。失败撤销本次映射前缀并退回槽，底层物理块仍归对象。

映射记录保存的 `maximumProtection` 来自原句柄权利；即使当前只读，也不能脱离原授权随意升为可写。`pagerManaged` 根据实际调用者身份记录，不接受用户自己声明。

### 4.4 unmapUserPages：为什么必须先准备再删除

例如原记录为 `[A,D)`，只删除中段 `[B,C)` 后，需要两条记录 `[A,B)` 和 `[C,D)`。如果先删页表，再发现没有第二个记录槽，账本就无法描述剩余映射。

因此顺序为：

1. 锁定目标空间，检查整个范围的每页都存在、属于用户且是普通小页。
2. 在锁页记录副本中模拟移除；内部替换页可要求保留锁页约定。
3. 先请 SharedMemory 预留拆分槽，再请 Maps 预留；后者失败则取消前者的预留，原映射保持不动。
4. 每批最多 64 页，先保存物理地址和原标志，再批量删除 PTE。
5. 等本地和远端 TLB 失效完成后，普通页归还物理分配器；external 页交给对象生命期管理，不能直接 free。
6. 提交锁页记录，再通过 `completeUnmap/releaseMapping` 更新对象记录和引用。

删除映射前缀时，页对象记录的虚拟起点和对象 offset 必须一起前移，否则剩余虚拟地址会指向错误的对象数据。

### 4.5 protectUserPages：先查权限上限，再改页表

先检查范围和写执行组合，锁目标空间；然后同时查询 SharedMemory 和 Maps 的权限限制，再预检每页存在和类型。检查通过后转换标志，每批最多 64 页更新，并完成 TLB 失效。物理地址、external、locked 和硬件访问状态由底层保留。

它修改的是已有映射权限，不负责分配缺失页面，也不能通过通用 protect 绕过 COW。

## 5. 锁页：登记哪些页应留在内存中

锁页包含两部分：页表上的 `pageLocked` 标记，以及进程中的锁页区间记录。后者能覆盖尚未实际补入的页面；将来映射这些地址时再设置对应页标记。

### 5.1 完整函数表

| 函数 | 输入、作用和结果 |
| --- | --- |
| `setPageLock(root, address, locked)` | 内部函数，找到驻留用户小页后设置或清除软件 locked 位，失败 false，不修改读写权限 |
| `lockUserPages(target, address, size, locked, allowAbsent, checkOnly)` | 对一个页对齐区间加锁或解锁；allowAbsent 允许未补页，checkOnly 只预检；返回 0/22/12 |
| `lockAllUserPages(target, flags, ranges, count)` | 位 0 锁当前范围，位 1 锁未来映射，0 解全部锁；ranges 为额外地址/长度对，返回 0/22/12 |
| `AddressSpace::Residency::contains(address)` | 判断地址是否属于任一锁页区间 |
| `AddressSpace::Residency::overlap(address, size)` | 累计与锁页记录重叠的字节数，不是区间数量 |
| `AddressSpace::Residency::set(address, size, locked)` | 在临时记录中合并或拆分区间，最多 128 段；成功整体提交，容量不足或范围溢出返回 false |
| `Residency::set` 内部 `RangeEmitter::operator()(begin, end)` | 将非空片段追加到临时表，相邻则合并，并维护累计字节数；容量满返回 false |

### 5.2 主要执行流程

`lockUserPages`：检查地址范围 → 锁目标空间 → 预检所有页，拒绝设备缓存类型和大页等不适用情况 → 在 Residency 副本中模拟修改 → 检查 `memlockLimit` → 若 checkOnly 则结束 → 修改已驻留页的软件位 → 提交区间记录。

`lockAllUserPages`：先在临时表中汇总驻留页和服务提供的额外区间，一次检查总额度；通过后才统一设置页标记并提交记录，最后单独设置 future 策略。仅 future 标志不会自动锁定当前所有页。

`Residency::set`：加锁时把重叠或相邻区间合并；解锁时保留删除范围以外的左右片段。整个新结果构造成功才替换旧表，因此记录空间不足时不会留下半份结果。

## 6. 地址空间创建、fork 和销毁

### 6.1 createAddressSpaceRoot

取得页表锁 → 分配顶层页 → 低 256 项清零，形成空用户半区 → 高 256 项复制内核根，沿用公共内核映射 → 返回新根物理地址。申请失败返回 0。

### 6.2 cloneAddressSpaceRoot 和上层 fork 的配合

这个函数接收已经创建好的目标根，只遍历源根用户半区：

1. 按四级表扫描有效叶子，遇到不支持的用户大页返回失败。
2. 普通私有页：申请新页，经直接映射复制完整 4 KiB 内容，再安装目标 PTE。
3. external 页：继续使用原物理地址，只复制映射关系。
4. 本轮安装失败时归还本轮新私有页，先前已安装的目标内容由调用者销毁目标根时清理。

仅复制 PTE 不足以完成 fork。子进程运行前还要配合 Maps::cloneTask、SharedMemory::cloneTaskMappings 等保留对象关系，并通过 PagerBridge::clone 同步用户态 MM 的区域状态；句柄也由任务层继承。

因此不能将这个底层克隆函数描述成“所有页面自动 COW”：普通私有页在这里深拷贝，external 页的共享和 COW 要结合上层对象与 MM 策略理解。

### 6.3 destroyAddressSpaceRoot

调用者先保证目标任务停稳、远端 CPU 不再使用旧空间，并协调设备 DMA 等外部访问。随后拒绝销毁内核根，锁页表，递归处理用户低半区：普通叶子释放 backing，external 叶子不直接 free，表页和根页按层释放。

Maps 和 SharedMemory 的记录要由各自清理接口结束引用，不能因为 PTE 已删除就忘掉对象，也不能同时由页表和对象重复释放 external 页。

`switchAddressSpace` 只修改本 CPU CR3，不意味着其他 CPU 已退出旧地址空间。

## 7. 用例子理解关键流程

### 7.1 地址翻译与用户权限

虚拟页首 0x400000 映射到物理页首 0x900000，访问 0x400123 就访问物理 0x900123；最低 12 位是页内偏移。大页使用更宽偏移，不能总是只保留 12 位。

CPU 会沿每级页表检查 user 权限，因此仅最后一级允许用户访问还不够，上级也要传播 pageUser。getOrCreateTable 正是同时负责建表和传播这个权限。

### 7.2 删除对象映射中间两页

原记录 `[0x400000, 0x408000)`，offset=0，删除 `[0x402000, 0x404000)` 后留下 2 页左段、4 页右段。右段虚拟起点为 0x404000，对象 offset 同时变成 0x4000。左右记录各保留一份对象引用，external 页不能因中间 PTE 消失而被逐页 free。

### 7.3 锁页区间合并和拆分

先锁 `[0x10000,0x12000)`，再锁相邻 `[0x12000,0x14000)`，记录可合成一段。解锁中间 `[0x11000,0x13000)` 后留下两段。Residency::set 先在临时表中构造完整结果，超过 128 段容量则不修改原表。

### 7.4 为什么不能先 free 再失效 TLB

若 CPU 1 仍缓存 `V → P`，CPU 0 把 P 归还并分给新使用者，CPU 1 就可能沿旧翻译修改新数据。正确顺序是删除 PTE → 完成相关 CPU 的旧翻译失效 → 再归还并复用 P。

## 8. 锁、返回值和调用者责任

目标地址空间锁保护根生命期和上层范围操作，全局页表锁保护表项，登记表锁保护对象引用。单页函数内部加锁，不代表调用者可以同时销毁其 root；Locked 辅助函数要求调用者已持页表锁。

批量 editPages 一次最多 64 页，上层可拆批处理更大范围。跨空间复制失败可能已复制前缀，克隆失败可能留下部分目标表，调用者需要清理失败目标。switchAddressSpace 只切当前 CPU，任务退出和 exec 还需上层协调远端 CPU 与 DMA。

多数单页接口返回 bool，地址翻译失败为 0；范围 map 成功为地址，unmap/protect 成功为 0，失败用 uint64_t 承载负错误码。锁页接口返回 0/22/12，具体区别见锁页章节。

## 9. 继续阅读

后备对象见 [对象与堆](object.md) 和 [共享内存](shared.md)，请求与缺页流程见 [内存机制服务](service.md)，总体依赖见 [内存总览](memory.md)。
