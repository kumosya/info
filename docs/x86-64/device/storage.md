# 引导块设备

内核存储代码目前是 `src/kernel/device/storage/boot_image.cc`，负责将引导模块作为内存块设备提供给用户态 storage 服务。BootImage 声明在 `include/kernel/device.h`，位于 `kernel::device`；没有独立的 kernel/storage.h。

## BootImage 保存的数据

对象保存模块字节指针 bytes、字节数 size 和复制操作锁 lock。`bootImage()` 返回共用对象；它引用已经保留的引导模块物理页，不复制整个文件系统映像。

`BootImage::init(boot)` 遍历 BootInfo 的模块，寻找命令名为 rootfs、后接字符串结尾或空格的模块。只有 end 大于 start 才接受，随后把起始物理地址转换为直接映射指针。没有匹配模块时容量保持为零，磁盘启动可以不使用这一设备。

## 块读写消息

`BootImage::call(request, reply)` 由 device.kernel 在确认 Storage 角色和 Device 能力后调用。它只解释引导镜像请求，不负责文件、路径或分区策略。

| 请求 | 参数 | 回复 |
| --- | --- | --- |
| `BootImageInfo` | 无额外参数。 | data[0] 为完整 512 字节块的数量，length 为 1。 |
| `BootImageRead` | data[0] 为起始块，data[1] 为块数。 | data[0] 为零，data[1] 为字节数，data[2] 起为内容。 |
| `BootImageWrite` | 同上，data[2] 起为待写内容。 | data[0] 为零，data[1] 为写入字节数，length 为 2。 |

每块固定 512 字节，即 64 个消息数据字；单次块数范围为 1～maximumStorageTransferBlocks，当前上限为 7。函数使用“总块数减起始块”的方式检查剩余容量，避免起始块加块数溢出；写请求还必须包含完整载荷。参数或范围无效返回 -1，不进入复制。

读写在 InterruptSpinLockGuard 内复制，防止多个请求交错修改同一内存区域。这只保证该次内存复制的串行访问，不代表文件系统事务或持久化。

## 与用户态存储的关系

物理 PCI 存储驱动在 `src/device/storage/`，设备认领、BAR 与 DMA 资源由[资源域](resource.md)提供，文件系统在 `src/fs/`。本文件不实现磁盘协议、分区解析或目录操作。

BootImageWrite 修改的只是内存中的 rootfs 模块，不能写回宿主 ISO 或原始磁盘映像。将这条路径称为持久磁盘写入会误导文件系统开发。
