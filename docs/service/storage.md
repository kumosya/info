# 存储服务（src/storage）

storage 管理 PCI 发现的 VirtIO Block，提供 512 字节块 I/O、刷新和分区枚举。fs 负责格式探测与挂载，storage 不指定根卷或光盘。

## 枚举和身份

每块整盘和每个有效分区都有独立节点，明确保存驱动索引、起始 LBA、长度、父设备和分区号。不再按节点序号猜测所属磁盘。

- 最多 8 个 VirtIO 控制器、32 个块节点。
- 整盘命名 vda、vdb，分区命名 vda1、vda2 等。
- 主号 252，次号为“控制器索引 × 256 + 分区号”，整盘分区号为 0。
- PCI 位置形成 pci-0000:BB:DD.F[-partN]，供 fs 提供 by-path。
- partition.h 解析 MBR 主分区与全部有效 GPT 分区，校验 GPT 头及分区表 CRC。
- GPT PARTUUID 来自唯一分区 GUID；MBR 使用磁盘签名和原始分区槽号。
- 相同 PCI IRQ 只绑定一次，按 PCI INTx 配置为低电平触发。内核先屏蔽该 IRQ，通知后检查各控制器完成队列并读取设备 ISR，最后使用绑定句柄调用 acknowledgeIrq 重新启用中断，避免未确认的高优先级中断压住调度时钟。
- 在分区探测发起首个 I/O 前启动中断处理线程，及时确认设备中断。

设备号和 vda 名称仅保证当前启动唯一。跨启动特指设备用 UUID/PARTUUID/LABEL/PCI 位置。fs 检测 ISO 后才生成 /dev/cdrom，只读不等于光盘。

## IPC

| 请求 | 语义 |
|---|---|
| GetCount | 状态和本次枚举的节点数 |
| GetInfo | 参数为节点 ID；前六字沿用状态、块数、块大小、传输限制、类型、只读，后附 StorageDeviceIdentity |
| ReadBlocks/WriteBlocks | 设备 ID 和相对 LBA，转换到对应驱动物理 LBA |
| Flush | 刷新节点所属物理设备 |
| ReadRange/WriteRange | 一次传输最多 7 个连续扇区，校验节点边界和消息长度；整批只提交一次 VirtIO 请求 |
| ReadAsync/WriteAsync | 完成端点、请求标识，校验分区范围并转换 LBA |

完成通知为 storageCompletionNotification。设备 ID 在请求首字高 8 位，低 56 位是相对 LBA。每次请求按明确映射路由并校验边界。

StorageDeviceIdentity 位于 include/os-c++/os/storage.h，包含节点 ID、主次号、父节点、分区号、起始块、名字、PARTUUID、PCI 路径。文件系统 UUID/LABEL 由 fs 读取卷元数据获得。

## 驱动与验证

VirtIO 1.0  transport 发现 common/notify/ISR/device capability，映射 BAR、协商只读/flush、分配 DMA 队列及请求缓冲。每个请求槽使用独立 DMA 页，可传输连续扇区，短暂轮询后才让出 CPU；提交与完成由锁保护。fs 的 BlockDeviceClient 用同步批量请求代替逐扇区传递完成端点，IPC 缓冲放在受锁保护的实例内，避免挤占启动线程栈。多设备采用独立 MMIO/DMA 区域。当前不包括 AHCI/NVMe、MBR 扩展分区链或热插拔。

可通过 `make mkimg` 创建 ext3/ext4/FAT32 分区镜像，再用 `make cpfiles run` 检查设备发现与挂载。
