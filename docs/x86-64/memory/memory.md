# 内存管理（src/kernel/memory/）

本文档描述 `src/kernel/memory/` 的实现：物理页分配器（`physical.cc`）、4 级页表虚拟内存（`virtual.cc`）、内核堆（`heap.cc`）、缺页处理（`fault.cc`）与 POSIX 风格命名共享内存（`shared_memory.cc`）。对应头文件 `include/kernel/mm.h`、`page.h`、`heap.h`。这个内存管理采取了最简化的实现，只是为了能够正常进行操作系统其他模块的编写，有空再来正式实现。

## 1. 虚拟内存布局

```text
0x0000000000000000 - 0x00007fffffffffff   用户空间（每进程独立，47 位）
0xffff800000000000 - 0xffff803fffffffff   物理内存直接映射（前 64 GB）
0xffff804000000000 - 0xffff804000ffffff   内核堆（16 MB）
```

- 用户地址有效区间：`[0x10000, 0x0000800000000000)`。
- 引导期建立的低端恒等映射在 `memory::initVir` 中被移除（清零 `PML4[0]`），此后仅 SMP trampoline 临时重建 0x8000 单页恒等映射。

## 2. 物理页分配器（physical.cc）

位图式分配器，管理最多 64 GB（16,777,216 页），所有函数位于 `namespace kernel::memory`：

- `pageBitmap[2 MB]`：占用位图（1 = 已占用）。
- `reservedBitmap[2 MB]`：保留位图（1 = 不可释放，如内核映像、Multiboot 结构）。

```cpp
namespace kernel::memory {
void initPhy(const BootInfo* bootInfo);
uint64_t allocatePhysicalPage();   // 返回物理地址，失败 0
void freePhysicalPage(uint64_t physicalAddress);
uint64_t getTotalMemoryBytes();    // 最高可用地址
uint64_t getFreeMemoryBytes();     // 空闲页数 × 4096
}
```

初始化流程：

1. 两个位图全部置 1（默认全部占用/保留）。
2. 遍历内存映射，`type == 1`（可用）的区域释放，并统计最高地址。
3. 依次保留：内核引导映像（`bootstrapStart..bootstrapEnd`）、Multiboot 信息结构、所有引导模块、framebuffer（`pitch × height`）。

分配策略：从**第 256 页（跳过前 1 MB）**开始线性扫描第一个空闲位。释放仅对"已占用且非保留"的页生效。全程由 `InterruptSpinLockGuard` 保护。

## 3. 页表标志（page.h）

| 常量                 | 位  | 含义                                     |
| ------------------ | -- | -------------------------------------- |
| `pagePresent`      | 0  | 存在                                     |
| `pageWritable`     | 1  | 可写                                     |
| `pageUser`         | 2  | 用户态可访问                                 |
| `pageWriteThrough` | 3  | 写穿透                                    |
| `pageCacheDisable` | 4  | 禁缓存                                    |
| `pageGlobal`       | 8  | 全局页                                    |
| `pageExternal`     | 9  | **软件位**：页帧由外部管理（共享内存/显存），克隆/释放地址空间时不回收 |
| `pageNoExecute`    | 63 | NX                                     |

## 4. 虚拟内存管理（virtual.cc）

4 级页表（PML4 → PDPT → PD → PT → 4 KB 页）。内核页表根保存于 `kernelAddressSpaceRoot`，全部操作由全局 `virtualMemoryLock` 保护。所有函数位于 `namespace kernel::memory`。

### 核心接口

```cpp
namespace kernel::memory {
void initVir(const BootInfo*);
bool mapPage(uint64_t va, uint64_t pa, uint64_t flags);          // 内核空间
bool unmapPage(uint64_t va);
uint64_t getPhysicalAddress(uint64_t va);

bool mapPageInAddressSpace(uint64_t root, uint64_t va, uint64_t pa, uint64_t flags);
bool unmapPageInAddressSpace(uint64_t root, uint64_t va);
bool protectPageInAddressSpace(uint64_t root, uint64_t va, uint64_t flags);
uint64_t getPhysicalAddressInAddressSpace(uint64_t root, uint64_t va, uint64_t* flags = nullptr);
bool validateUserMemory(uint64_t root, uint64_t address, uint64_t size, bool writable);
UserPageMapResult allocateUserPageInAddressSpace(uint64_t root, uint64_t va, uint64_t flags);
bool copyToAddressSpace(uint64_t root, uint64_t destination, const void* source, uint64_t size);
bool copyFromAddressSpace(void* destination, uint64_t root, uint64_t source, uint64_t size);

uint64_t getKernelAddressSpaceRoot();
uint64_t createAddressSpaceRoot();
bool cloneAddressSpaceRoot(uint64_t srcRoot, uint64_t dstRoot);
void destroyAddressSpaceRoot(uint64_t root);
void switchAddressSpace(uint64_t root);
}
```

### 关键行为

- **大页处理**：映射 4 KB 页时若 PD 级已存在 2 MB 大页，自动拆分为 512 个 4 KB 页（`splitLargePage`，保留原标志并清除 PS 位）。不支持创建 1 GB/2 MB 新映射，也不支持解除大页映射。地址翻译（`translateLocked`）支持 1 GB 与 2 MB 大页。
- **`initVir`**：要求 `bootInfo->pageTableRoot != 0`；清零 `PML4[0]` 移除低端恒等映射并重写 CR3。
- **`mapPageInAddressSpace`**：va/pa 必须页对齐；成功后本地 `invlpg`。
- **`unmapPageInAddressSpace`**：成功后本地 `invlpg` + `smp::requestTlbShootdown(va)` 通知其他核。
- **`protectPageInAddressSpace`**：要求 `flags` 含 `pageUser`；修改已存在表项标志（保留物理地址位与 `pageExternal` 位）；成功后 `invlpg` + TLB shootdown。
- **`validateUserMemory`**：区间末端必须 < 用户上限且不回绕；逐页校验 `pageUser`（需要时还要 `pageWritable`）。
- **`allocateUserPageInAddressSpace`**：参数校验（页对齐、`va >= 4096`、用户半区、必须 `pageUser`、**可写页必须同时带** **`pageNoExecute`**，即 W^X）；分配物理页并经直接映射清零；返回 `Success / InvalidArgument / OutOfMemory / MappingFailed`。
- **`copyToAddressSpace`** **/** **`copyFromAddressSpace`**：范围校验后持锁逐页翻译，要求 `pageUser`，经直接映射分块复制。
- **`createAddressSpaceRoot`**：分配 PML4；低 256 项清零（用户半区），高 256 项从内核 PML4 复制（内核半区共享）。
- **`cloneAddressSpaceRoot`**（fork 用）：深拷贝用户半区；遇到大页直接失败；非 `pageExternal` 的叶页分配新物理页并逐字节复制 4096 字节；保留 `Writable|User|WriteThrough|CacheDisable|Global|External|NoExecute` 标志；失败时回收已分配副本。
- **`destroyAddressSpaceRoot`**：拒绝释放内核根；递归释放低 256 项指向的用户页表树；非 `pageExternal` 叶页释放物理页。
- **`switchAddressSpace`**：root 为 0 回退内核根；与当前 CR3 不同才写。

## 5. 内核堆（heap.cc）

内核堆使用按需映射的空闲块分配器，所有函数位于 `namespace kernel::heap`：

```cpp
namespace kernel::heap {
void init();
void* kernelAllocate(size_t size);
void kernelFree(void* address);   // 释放并合并相邻空闲块
}
```

- 基址 `0xffff804000000000`，上限 16 MB。
- 分配 16 字节对齐；`heapMappedEnd` 不足时循环 `memory::allocatePhysicalPage` + `memory::mapPage(pageWritable | pageNoExecute)` 扩页。
- 分配优先复用空闲块并按需拆分；释放合并相邻空闲块。映射的物理页留在堆内供复用。
- 退出线程的内核栈在切换到其他栈后释放；仍有同步 IPC 回复可能写入的栈暂时保留，避免回复访问已复用的内存。任务/线程对象仍保留供现有句柄与授权记录引用。

## 6. 缺页处理（fault.cc）

```cpp
namespace kernel::interrupt {
void handlePageFault(interrupt::Context* context);
}
```

1. 读 CR2 得到故障地址；串口打印故障地址、错误码、RIP。
2. 来自用户态（`cs & 3 != 0`）：打印后调用 `Thread::terminateCurrentFromException(-14)`。
3. 内核态：打印 `[MEMORY] kernel page fault`。
4. 最后 `cpu::halt()`。

**当前实现限制**：无论用户/内核故障，处理路径最终都会停机。

## 7. 命名共享内存（shared\_memory.cc）

POSIX `shm_open` 风格，三级注册表（对象/句柄/映射），全部静态分配。接口通过 `kernel::SharedMemory` 类的静态方法提供：

| 容量      | 值          |
| ------- | ---------- |
| 对象数     | 16         |
| 全局句柄数   | 64         |
| 全局映射数   | 64         |
| 单对象最大页数 | 64（256 KB） |
| 名称最大长度  | 64         |

### 接口

```cpp
class SharedMemory {
   public:
    static uint64_t open(Task& task, const char* name, int flags);
    static uint64_t duplicateHandle(Task& source, Task& destination,
                                    uint64_t handle, uint64_t destinationHandle = 0);
    static bool unlink(const char* name);
    static bool resize(Task& task, uint64_t handle, uint64_t size);
    static uint64_t map(Task& task, uint64_t handle, uint64_t address,
                        uint64_t size, uint64_t offset, uint64_t protection);
    static bool releaseMapping(Task& task, uint64_t address, uint64_t size);
    static bool protectionAllowed(Task& task, uint64_t address, uint64_t size,
                                  uint64_t protection);
    static void releaseTaskMappings(Task& task);
    static bool cloneTaskMappings(Task& source, Task& destination);   // fork 用
    static void closeHandleObject(void* object);                      // 句柄关闭回调
};
```

### 关键规则

- 名字必须以 `'/'` 开头；访问模式只读（0）或读写（2）；支持 `O_CREAT`（0x40）与 `O_EXCL`（0x80）。
- 权限：只读给 `handleRightRead`；读写追加 `Write | Manage`。
- `resize`：需 Manage 权限；上限 256 KB；**有映射存在时不允许缩小**；增长分配并清零物理页，缩小释放尾部页。
- `map`：读权限必需，请求可写需写权限；address/size/offset 页对齐；`protection` 只能是 0/1/2（3 非法）；页标志 `pageUser | pageExternal`（+按需 `pageWritable`/`pageNoExecute`）。`pageExternal` 使这些页帧在地址空间克隆/销毁时不被复制/回收。
- 锁顺序：先进程地址空间锁（`PreemptionSpinLockGuard`），再注册表锁（`InterruptSpinLockGuard`）。
- 对象回收条件：已 unlink 且 `references == 0 && mappings == 0`。
- `releaseMapping` 由 `UnmapMemory` 系统调用在解除映射后调用，按 (进程, 地址, 大小) 精确匹配。
