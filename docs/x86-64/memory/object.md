# 内核对象和堆分配

声明位于 `include/kernel/mm.h`、`page.h`，命名空间为 `kernel::mm`。实现为 `heap.cc`、`object/slab.cc`、`per_cpu.cc`、`pool.cc`、`mapping.cc`、`page_object.cc`，以及 `memory.S`。

## 内核堆的选择和释放

heap.cc 有七种包含 Header 的大小分类：32、64、128、256、512、1024、2048 字节。Header 为 16 字节对齐前缀，记录分配标识、缓存分类和大块阶数；返回的 payload 位于它之后。

`initHeap()` 初始化缓存。`allocateHeap(size)` 将头部加入所需大小，选择能容纳的最小缓存；更大对象申请连续 buddy 块并建立稳定别名。别名失败时释放刚取得的块，不能只丢掉返回指针。

`freeHeap(object)` 回退头部，核对堆别名、物理块状态及 owner，再通过原子比较交换领取 allocatedMagic。只有成功领取者能够交回缓存或大块分配器，避免两个释放者把同一对象重复挂链。该检查不意味着可以传入任意未映射地址。

大块以 bucket=UINT16_MAX 区分，释放要求块头对齐且记录阶数匹配。小对象交回每 CPU 缓存。它们没有自动清零或 C++ 析构语义，调用者负责初始化和对象销毁。

## ObjectCache 与 Slab

ObjectCache 保存对象 stride、slab 链、空 slab 数和锁。私有 Slab 位于一个 4 KiB 页开头，保存所属 cache、链链接、空闲槽头、used 位图、容量和 live 数。空闲槽用自己的前几个字节保存 next 指针，分配后这些字节属于调用者。

| 成员函数 | 功能与参数 |
| --- | --- |
| `init(objectSize)` | 将支持的对象大小向上对齐到 16 字节，初始化空链。 |
| `allocate()` | 在有空位的 slab 弹出槽；没有时分配并初始化整页，再提供对象。 |
| `release(object)` | 定位页头，检查 cache、槽边界和 used 位，清位后压回空闲链。 |
| `shrink()` | 释放全部当前空 slab，返回页数。 |
| `unlink(slab)` | 在 cache 锁内只摘链，不独自决定物理页生命期。 |

live 由 0 变 1 时减少 empty；释放最后对象时保留一个空 slab 便于复用，多余空 slab 才立刻交回 buddy。used 位用于拒绝重复释放，不能仅凭“指针位于这个页中”就接受它。

## PerCpuObjectCache

该类包含共享 ObjectCache 和每 CPU 的有界 magazine。magazine 暂存的槽仍被 slab 视为 live，直到 drain 真正交回 slab。

`allocate()` 先关本 CPU 中断，从本地 magazine 取对象，失败则在释放 magazine 锁后进入共享 cache。`release(object)` 优先放入释放者所在 CPU 的 magazine，满时交回共享 cache；对象不要求回到最初分配它的 CPU。

`drain()` 逐个锁定 magazine 并交回槽，然后 shrink，返回页数。`reclaimCaches()` 汇总并转换成字节；必须先 drain 再 shrink，否则暂存对象会阻止本来可回收的页变空。

`enableCpuObjectCaches()` 在每 CPU 数据可用后启用本地索引，早期使用槽 0。该设计减少共享 cache 争用，并不取消跨 CPU 释放和集中回收所需的锁。

## FixedPool

FixedPool 使用调用者提供的固定存储，不增长。状态包括 storage、stride、capacity、free 数、空闲首索引、used 位图和锁。

`init(storage, bytes, size)` 检查对齐、整数范围和容量，建立槽内空闲索引；`allocate()` 弹出索引并置 used 位，返回原始存储；`release(object)` 验证精确槽边界和 used 位后压回；`available()` 返回剩余数量。容量耗尽返回空，不隐式转向另一种分配器。

## PageObject 与物理块引用

`PageObject(order, owner, node)` 直接申请连续物理块并清零；对象存储分配成功仍可能没有页 backing，调用者必须检查 valid。数据读取涉及 physical、size、owner、order 和 references。

`retain()` 同时增加对象引用和物理块引用，使映射能独立于创建句柄存活。`release()` 的非最后一次释放减少配对物理引用；最后一次运行析构，释放最后块引用，再释放包装对象。不能在最后分支额外重复释放物理块。

映射注册表保存自己的对象引用。pageExternal PTE 只借用物理地址，因此页表销毁跳过这些叶子的普通页释放，由注册表完成对象引用回收。

## 稳定映射与内存原语

`mapKernelPages(physical, order)` 在 `0xffff804000000000 + physical` 建立可写、不可执行别名。已有相同映射可以复用，冲突映射失败；它不转移物理所有权，也不承诺失败时删掉已建立的别名。

`physicalFromKernelPointer(address)` 只做专用别名区间换算；`clearPhysicalPages(physical, bytes)` 经直接映射清零物理字节。范围换算不是映射存在性或调用者拥有权证明。

memory.S 提供 memset、memcpy、memmove。前两者按清除的 DF 向前操作；memmove 根据地址关系选择方向，倒序后必须 cld 恢复 C ABI 要求。memcpy 不负责重叠区域。
