# 对象与内核堆：从整页到几十字节

本文覆盖 `heap.cc`、`object/slab.cc`、`per_cpu.cc`、`pool.cc`、`mapping.cc`、`page_object.cc` 和 `memory.S` 的函数。实现位于 `src/kernel/memory/`，声明位于 `include/kernel/mm.h`、`page.h`。C++ 接口属于 `mm`，汇编基础函数保留 C 链接名。

## 1. 为什么一页上还要分对象

如果 40 字节结构体独占一张 4 KiB 页，会浪费大量空间。因此内核把页切成多个对象槽，再给每个 CPU 暂存少量槽，减少公共锁竞争。

```text
allocateHeap(字节数)
  ├─ 小对象：CPU 暂存 → 公共 ObjectCache → slab 中的槽
  └─ 大对象：连续物理块 → 内核堆固定别名
```

| 概念 | 通俗解释 |
| --- | --- |
| Header | 返回数据前面的分配记录，保存释放时所需的来源信息 |
| stride | 两个对象槽首地址之间的距离，按 16 字节对齐 |
| slab | 一张物理页，开头放管理头，其余切成等大对象槽 |
| magazine | CPU 的小暂存栈，每区最多 8 个对象 |
| FixedPool | 只划分外部给定存储，不会扩容的槽位池 |
| PageObject | 包装连续物理页块，用引用计数管理使用关系 |

## 2. 启动时准备缓存

### 2.1 initHeap：准备大小档位

初始化 32、64、128、256、512、1024、2048 字节七档缓存，各档包含自己的管理头。此时只是准备缓存状态，slab 物理页在真正申请时按需取得。

随后 `cpu::init()` 建立 CPU 局部数据，再调用 `memoryTopology().enableCpuLocal()` 和 `enableCpuObjectCaches()`。此前节点选择和 CPU 暂存索引使用启动默认值 0。

## 3. 内核堆和对象缓存

### 3.1 allocateHeap / freeHeap 的执行流程

`allocateHeap(size)`：

1. 拒绝零长度和过大申请，保证加管理头不会溢出。
2. 计算 `required = size + sizeof(Header)`，管理头为 16 字节。
3. 从七个大小档位中选能容纳 required 的最小档。例如申请 40 字节，合计 56 字节，选择 64 字节档。
4. 小对象先从当前 CPU 暂存取；没有时进入公共 slab 缓存；公共缓存也满了才申请新物理页。
5. 超过 2048 字节档时，计算足以容纳 required 的最小阶数，申请连续物理块，再建立可写、不可执行的堆别名。别名失败则归还刚申请的物理块。
6. 在 Header 记录分配标记、档位或阶数，返回 Header 后的数据地址。

返回区域保持 16 字节对齐，但不自动清零，也不自动调用 C++ 构造函数。

`freeHeap(object)`：

1. 空指针直接忽略，不满足 16 字节对齐则返回。
2. 向前退一个 Header，换算堆别名对应的物理地址。
3. 查询底层物理块，要求仍为已分配状态且 owner=0。
4. 用原子比较交换把“已分配”标记改成“已释放”，只有成功认领这次释放的线程才能继续。
5. 小块退回当前 CPU 暂存；暂存满时交回公共 slab。
6. 大块还要核对页首对齐和原始阶数，再归还物理块。

这些检查用于发现常见错误，不代表可以传任意地址，也不能保证识别地址被重新分配后的所有悬空指针。调用者应传原分配结果并负责对象析构。

### 3.2 ObjectCache 的页内分配流程

一页布局为“Slab 管理头 + 若干等间距对象槽”。未使用的槽用自己的前几个字节保存下一个空槽地址；槽被分配后，这些字节就归调用者使用。

`allocate()` 先找有空槽的 slab，没有则申请并映射一页，初始化管理头、占用位图和空槽链；随后弹出一个槽，置占用位，增加活对象数。失败返回 nullptr。

`release(object)` 向下按 4 KiB 对齐找到管理头，核对所属缓存、精确槽边界和占用位，再清位、把槽压回空闲链。某页变空时，默认保留一页供复用，多余全空页立即归还。

CPU 暂存中的对象仍被 slab 视为活对象。因此“调用者已经 free”不一定等于“物理页已经空闲”。主动回收必须先清 CPU 暂存，再回收全空 slab。

### 3.3 堆与缓存完整函数表

| 函数 | 输入、作用和结果 |
| --- | --- |
| `initHeap()` | 初始化七个大小档位，初始化流程见 [总览](memory.md) |
| `allocateHeap(size)` | 按字节申请，返回数据指针或 nullptr |
| `freeHeap(object)` | 读取前置管理头，按来源归还，不返回结果 |
| `reclaimCaches()` | 对七档依次 drain，返回汇总得到的回收字节数 |
| `ObjectCache::init(objectSize)` | 接受 1～2048 字节对象，将槽间距向上对齐到 16 字节；初始化锁和空链 |
| `ObjectCache::allocate()` | 返回一个槽地址，没有资源时 nullptr |
| `ObjectCache::release(object)` | 归还精确槽地址，重复释放或来源不符返回 false |
| `ObjectCache::unlink(slab)` | 只从缓存双向链摘页，不单独决定释放时机 |
| `ObjectCache::shrink()` | 释放当前全空 slab，返回页数；注意不是字节数 |
| `ObjectCache::Slab::hasSpace()` | 根据空槽链头判断是否有空位 |
| `enableCpuObjectCaches()` | CPU 局部数据可用后启用真实 CPU 索引 |
| `PerCpuObjectCache::currentCpu()` | 未启用时 0，启用后用当前 CPU ID 对 64 取模 |
| `PerCpuObjectCache::init(objectSize)` | 初始化公共缓存和 64 个 CPU 暂存区，每区最多 8 个对象 |
| `PerCpuObjectCache::allocate()` | 短暂关中断并取当前 CPU 暂存；没有则放锁后从公共缓存申请 |
| `PerCpuObjectCache::release(object)` | 空指针忽略；当前 CPU 暂存未满则入栈，否则放锁后退回公共缓存 |
| `PerCpuObjectCache::drain()` | 逐个清空 CPU 暂存并调用 shrink，返回最终 shrink 的页数 |
| `PerCpuObjectCache::Magazine::take()` | 弹出暂存区最后一个对象，空时 nullptr；锁由调用方保证 |

`drain` 过程中归还对象也可能触发 slab 即时释放；它的返回计数来自最后的 `shrink()`，因此不要将返回值当作并发情况下所有物理释放事件的精确审计值。

### 3.4 FixedPool：固定容量的对象池

`FixedPool` 不申请新 backing。调用者先提供一块存储，它负责将存储切成等大槽位，最多 4096 槽，耗尽直接失败。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `init(storage, bytes, objectSize)` | 检查存储 16 字节对齐和容量，把槽大小向上对齐，清位图并建立空闲索引链；成功 true |
| `allocate()` | 弹出空闲索引、置位、减少空闲数，返回槽地址；耗尽 nullptr |
| `release(object)` | 核对精确槽首和占用位，清位并压回空闲链；无效或重复释放 false |
| `available()` | 返回剩余槽数，不是剩余字节数 |

它与 slab 的区别是容量固定、存储由外部提供；例如 `Maps` 用它管理预置的映射记录数组。

### 3.5 内核访问地址与基础复制函数

| 函数 | 输入、作用和结果 |
| --- | --- |
| `mapKernelPages(physical, order)` | 为整个块逐页检查/建立 `heapBase + physical` 的可写 NX 别名；已指向同一页则复用，冲突或建表失败返回 nullptr |
| `clearPhysicalPages(physical, bytes)` | 通过直接映射逐字节清零；参数是调用者已保证可写的物理范围 |
| `physicalFromKernelPointer(address)` | 仅接受堆固定别名区域，减去 heapBase 得物理地址；范围外返回 0，不检查实际 PTE 是否存在 |
| `memset(destination, value, size)` | 将低 8 位字节值重复写入目标，返回原目标地址 |
| `memcpy(destination, source, size)` | 向前复制，返回原目标；源目标不能重叠 |
| `memmove(destination, source, size)` | 根据地址关系选择正向或倒向复制，允许重叠，返回原目标 |

`mapKernelPages` 不接管物理块所有权，失败可能留下本次已建立的固定别名；调用者仍要归还自己申请的块。`memmove` 倒向复制使用方向标志 DF，返回前恢复为正向；正向字符串指令依赖调用约定中的 DF=0。

## 4. PageObject：让页面跟随使用关系存活

直接分配的物理地址只是一串数字，不能自动表达“句柄和映射都还在使用这块内存”。`PageObject` 给连续物理块加了一层引用计数包装。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `PageObject(order, owner, node)` | 最多接受 12 阶，申请连续物理块并清零；申请失败仍构造出包装，但 valid 为 false |
| `~PageObject()` | 私有析构，归还最后一份物理块引用 |
| `valid()` | 物理地址非 0 即为有效 |
| `retain()` | 增加物理块引用和对象引用，供新映射等长期使用关系持有 |
| `release()` | 对象引用减一；非最后引用归还对应物理引用，最后引用负责析构并 freeHeap 包装 |
| `size()` | 有效时返回 `4096 << order` 字节，否则 0 |
| `physical()/owner()/order()` | 读取物理首地址、owner 和阶数 |
| `references()` | 原子读取对象引用计数 |
| 复制构造、复制赋值 | 已删除，禁止按值复制产生两份互不协调的管理对象 |

典型流程：申请包装 → 构造并检查 valid → 安装句柄 → 建立映射时 retain → 关闭句柄时 release → 最后映射消失时 release → 物理块和包装都被归还。

最后一次 release 不额外归还两次物理引用：最后那一份由析构函数处理。

## 5. 把主要路径串起来

### 5.1 申请 40 字节

1. allocateHeap 加 16 字节 Header，合计 56 字节，选择 64 字节档。
2. 先从当前 CPU 暂存取槽，没有则进入公共 ObjectCache。
3. 公共缓存找已有空槽，没有才申请整页、建立别名并初始化 slab。
4. 堆写好 Header，调用者拿到后面的数据指针。
5. freeHeap 校验和认领释放后，把整个槽退回当前 CPU 暂存。
6. 暂存满时退公共缓存；页面全部空闲且超过保留数量时才归还物理页。

CPU 0 分配、CPU 1 释放是允许的，归还时用释放者所在 CPU 的暂存区。

### 5.2 申请 3000 或 5000 字节

3000 加管理头后超过小对象最大档，但可放进一页，申请 0 阶。5000 加管理头需要两页，申请 1 阶。大块的 Header 用 bucket=UINT16_MAX 标识，释放必须按保存的原始阶数归还。

### 5.3 FixedPool 如何用槽内索引建链

4 个槽初始化为 `0 → 1 → 2 → 3 → 结束`。allocate 弹出 0 并设置占用位，下次从 1 开始；release(槽0) 清位后将 0 压回链头。再次释放槽0 时占用位已清，返回 false。容量用尽返回 nullptr，不扩容。

### 5.4 reclaimCaches 如何处理暂存对象

逐档 drain，把各 CPU 暂存交回公共缓存，再 shrink 全空页，最后将返回页数换算为字节。只有部分槽空闲的页不能整体归还。返回值的计数边界见前面的函数表说明。

### 5.5 PageObject 的正确创建顺序

allocateHeap 准备包装存储 → placement new 构造 → 检查 valid → 安装句柄 → 映射 retain → 关闭句柄或解除映射 release → 最后引用析构并释放包装。不能直接把包装存储分配成功当成物理页申请成功，也不能在最后分支再 free 一次 backing。

## 6. 使用时需要分清的保证

普通堆申请不自动清零、构造或析构 C++ 对象。PageObject 构造会清零自己的 backing。physicalFromKernelPointer 只换算专用堆别名，不接受所有高半区指针。mapKernelPages 失败可能留下部分固定别名，但物理块仍归调用者负责。

memmove 可以处理重叠：将数组 `[0,4)` 搬到 `[1,5)` 时先从高地址复制，避免覆盖尚未读取的源数据。memcpy 不提供这个保证。双重释放检查也不能代替正确生命期管理，尤其无法识别地址复用后的所有悬空指针。

## 7. 继续阅读

物理块规则见 [物理内存](physical.md)，对象映射引用见 [虚拟内存](virtual.md)，用户请求见 [内存机制服务](service.md)。
