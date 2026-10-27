# DMA 缓冲区

`src/kernel/device/dmabuffer/dmabuffer.cc` 实现 `kernel::device::DmaBuffer`，声明在 `include/kernel/device.h`。对象保存所属资源域、域内链表指针、映射与关闭标志，以及物理地址、用户地址和实际分配大小。

## 分配与映射

`DmaBuffer::create(task, domain, userAddress, info, size, alignment)` 为 task 分配缓冲区。domain 是已认领设备的资源域；userAddress 为零时自动选择地址；info 接收用户地址、设备地址和实际大小。默认 size、alignment 都是 4096，失败返回空指针。

size 和 alignment 均不得超过 8 MiB，大小非零，对齐必须是非零的二次幂。函数选择同时满足大小与对齐的 Buddy 阶数，因此实际分配可能大于请求值，调用者以 info.size 为准。

执行顺序如下：

- 确认资源域仍由 task 持有且处于 Active，计算实际块大小，校验用户地址对齐和范围。
- 持地址空间锁取得页表根，调用物理块分配器，使用 task.id() 记录物理块归属。
- 逐页建立用户可写、不可执行的 external 映射。external 表示 PTE 撤销时不能按普通页面归还底层块。
- 分配 DmaBuffer 包装对象，清零整个物理块，再挂入资源域。挂入时检查域状态以及每域最多 2048 页的总量。
- 发布 info。device.kernel 随后建立句柄并把句柄加入回复。

任何一步失败，都会撤销本次 PTE，并按原阶数和所有者释放物理块；包装对象已经分配时也一并释放。资源域拒绝 attach 时同样回滚，不能把未登记的 DMA 块交给驱动。

## 关闭与真正回收

`destroy()` 使用原子交换保证只执行一次关闭，撤销仍对应原地址空间和物理块的用户映射，并减少域引用。它不会直接释放连续物理块，也不会立即释放仍挂在域链表上的包装对象。

原因是设备可能已经保存了 DMA 地址。CPU 不再访问缓冲区，甚至用户 PTE 已经撤销，都不能证明设备停止了 DMA。资源域工作线程必须先复位设备并确认 DMA 停止，才可释放物理块。复位失败时资源进入隔离状态，保留物理页，见[资源域](resource.md)。

当前 info.deviceAddress 直接使用物理地址，没有 IOMMU 地址转换、散列表 DMA 或通用非一致性缓存维护。因此该实现的硬件适用范围受当前平台与驱动约束，不能当作完整的跨架构 DMA API。
