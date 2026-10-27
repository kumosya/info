# 页表、映射与地址空间

实现位于 `virtual/virtual.cc`、`page_mappings.cc`、`operations.cc`，声明在 `include/kernel/page.h`，命名空间为 `kernel::mm`。它们分别负责硬件页表、对象映射引用和面向 Task 的范围操作。

## 四级页表与地址翻译

PageTable 是固定硬件布局，一页 512 个 64 位表项。表项保存物理地址及标志，C++ 使用直接映射访问表页。`getTable` 去掉标志并加直接映射基址，`getTablePhysicalAddress` 完成反向换算。

`allocateTable()` 先清零再交给父表发布。`getOrCreateTable(parent, index, flags, owner)` 在缺失时创建下级表，并将 user 权限传播到上层，否则仅叶子置 user 仍无法由硬件访问。

`translateLocked(root, address, flags)` 支持 1 GiB、2 MiB、4 KiB 叶子的偏移，flags 返回实际叶子标志。普通用户机制建立 4 KiB 页；`splitLargePage(directory, index)` 可把 2 MiB 叶子展开成准备完毕的 512 个小页后再替换父项，不表示任意大页操作都已实现。

## 单页操作与锁

| 函数 | 功能与参数 |
| --- | --- |
| `mapPageInAddressSpace(root, virtualAddress, physicalAddress, flags)` | 输入对齐地址，创建必要级别并安装不存在的叶子，拒绝覆盖已有映射。 |
| `unmapPageInAddressSpace(root, virtualAddress)` | 清除现有小页叶子，失效本地及远端翻译，不自动释放物理 backing。 |
| `protectPageInAddressSpace(root, virtualAddress, flags)` | 改用户页权限，保留物理地址、external 和 accessed/dirty 状态，并请求失效。 |
| `getPhysicalAddressInAddressSpace(root, virtualAddress, flags)` | 翻译为物理字节地址，可取得叶子状态。 |
| `validateUserMemory(root, address, size, writable)` | 检查全部覆盖页的 resident/user 及可选写权限；零长度允许，不自动缺页。 |
| `allocateUserPageInAddressSpace(root, address, flags)` | 申请并清零单页，失败回收，返回 UserPageMapResult 分类。 |

这些函数用全局 virtualMemoryLock 串行访问表项，但目标 root 的生命期由上层地址空间锁或启动阶段保证。不能用“函数内部加锁”推断 root 不会被并发销毁。

mapPage、unmapPage、getPhysicalAddress 是内核根包装。解除映射和降权需要远端 TLB shootdown；只改 RAM 中 PTE 不足以阻止其他 CPU 沿旧翻译访问或写入。应在必要失效完成后才能释放、复用 backing。

## Maps 与 Map

Maps 保存有界 FixedPool、Map 链和锁。Map 保存目标 Task、PageObject 引用、虚拟范围、对象偏移、最大保护权限、pagerManaged 与 cow。

`map(target, object, address, size, offset, protection, maximumProtection)` 先按地址空间、注册表顺序加锁，校验对象范围、重叠和权限，再准备记录、逐页映射。成功后保留对象并发布记录；失败只撤销本次前缀并退回记录。

`protectionAllowed` 检查所有重叠记录的最大权限和 COW 限制。`markCopyOnWrite` 要求精确记录，清 write 并失效后登记 cow；普通 protect 不能绕过它恢复 write。

`cloneTask(source, target)` 先预检记录容量，再为子进程复制记录并逐个 retain。`releaseTask(target)` 释放记录引用，不代替页表销毁。`dumpAllowed`、`nextManagedMapping` 仅认可经过真实 Memory 服务建立的 pagerManaged RAM。

## 范围操作和解除映射事务

`mapUserPages(target, address, size, protection)` 实现有界的立即分配匿名映射。范围及大小按 4 KiB 对齐，单次最多 64 MiB；保护低位分别为 write=1、execute=2，拒绝同时设置。失败撤销本次已映射前缀，不替换原有叶子。

`unmapUserPages` 先检查整个区间的页存在，再分别调用 SharedMemory 和 Maps 的 prepareUnmap。严格挖掉一个记录中段会留下左右两段，因此必须在删除 PTE 前预留第二个记录；若另一注册表准备失败，取消已预留槽，不动原映射。

PTE 删除后，普通页归还物理分配器；external 页保留由对象所有者管理。随后 completeUnmap、releaseMapping 删除、裁剪或拆分记录。PageObject 前缀裁剪还要同步对象 offset，不能只移动虚拟起点。

`protectUserPages` 先校验对象权限上限和所有页，再逐页改权限。它没有把完整用户态 VMA 或文件同步策略搬进内核，也不提供 MAP_FIXED 替换事务。

## 地址空间创建、克隆和退出

`createAddressSpaceRoot()` 新建空的用户低半区，复制共享内核高半区的根项。`cloneAddressSpaceRoot(sourceRoot, destinationRoot)` 对普通私有页做深拷贝，对 external 页复制物理地址；后者必须另外继承对象记录，不能只复制 PTE 便开放子线程执行。克隆失败可能留下部分目标根，由创建者销毁。

`destroyAddressSpaceRoot(root)` 仅递归释放私有用户表页和非 external 叶子，拒绝内核根。执行前应完成 Task 停稳和外部资源协调。`switchAddressSpace(root)` 只切本 CPU CR3，不能证明其他 CPU 已离开目标空间。

`copyToAddressSpace`、`copyFromAddressSpace` 经物理直接映射逐页跨根复制。前者用于加载准备，不自行执行 writable/COW 策略；不可信用户写入仍需 syscall/Pager 检查。失败可能已复制前缀，调用者不能把部分数据发布为完整成功。

`nextDumpPage` 只遍历驻留用户小页，跳过硬件缓存类型与大页设备别名；core 对 external RAM 还会查询 Maps 许可，见 [core](../task/core.md)。
