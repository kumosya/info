# 存储服务（src/device/storage）

storage 管理 PCI 发现的 VirtIO Block，以及 GRUB 从光盘加载的 rootfs 内存块设备，提供 512 字节块 I/O、刷新和分区枚举。fs 负责格式探测与挂载。

纯光盘启动时，ISO 中包含完整 /boot/rootfs.ext3，GRUB 通过 module2 加载并命名 rootfs。Ring0 的 `src/kernel/device/storage/boot_image.cc` 仅允许固定 storage 身份访问该保留模块，校验扇区范围并复制数据；Ring3 的 `include/service/device/storage/boot_image.h` 将其接入现有存储协议，节点为 /dev/ram0，类型 BootImage，主号 1，路径身份 boot-rootfs。根卷通过 LABEL=OSINFO_ROOT 挂载，写入只在本次启动内存中保留。

## 枚举和身份

每块整盘和每个有效分区都有独立节点，明确保存驱动索引、起始 LBA、长度、父设备和分区号。不按节点序号猜测所属磁盘。

- 最多 8 个存储后端、32 个块节点；rootfs 内存模块存在时占一个后端槽位。
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
| GetStatistics | 参数为节点 ID。回复为状态、已完成请求、已确认读取字节、已确认写入字节、错误、超时、在途请求，共七字 |
| ReadBlocks/WriteBlocks | 设备 ID 和相对 LBA，转换到对应驱动物理 LBA |
| Flush | 与同一父设备的写入串行；返回具体 storage 状态 |
| ReadRange/WriteRange | 每批上限以 GetInfo 为准；回复首字为状态，已确认连续前缀存在时第二字为字节数，读回复附对应数据 |
| ReadAsync/WriteAsync | 完成端点、请求标识，校验分区范围并转换 LBA。通知只尝试发送一次，端点满时丢弃通知 |

完成通知为 storageCompletionNotification。设备 ID 在请求首字高 8 位，低 56 位是相对 LBA。每次请求按明确映射路由并校验边界。

统计按父后端归集，以工作线程收到的读、写或刷新 IPC 为单位。客户端拆分后的每批计一次，内部队列与分区探测不重复计数。无法识别目标的请求和 SDK 在本地拒绝的请求不计入设备统计。超时计数来自服务处理结果，客户端提前结束 IPC 等待不会另加一次。异步通知投递失败不改变对应 I/O 的结果。统计在服务重启后重新开始。

StorageDeviceIdentity 位于 `include/service/common/records.h`，包含节点 ID、主次号、父节点、分区号、起始块、名字、PARTUUID、PCI 路径。文件系统 UUID/LABEL 由 fs 读取卷元数据获得。

刷新覆盖已进入同一父设备写入顺序的请求。写穿透设备可在确认此前写入完成后直接成功，写回设备须等硬件刷新完成。缺少刷新保证时返回 `NotSupported` 或 `CapabilityUnknown`。BootImage 的刷新只保证本次运行中的内存可见性。未知结果的批次不计入已确认前缀，客户端在首次失败后停止拆批。

## 驱动与验证

VirtIO 1.0  transport 发现 common/notify/ISR/device capability，映射 BAR、协商只读/flush、分配 DMA 队列及请求缓冲。每个请求槽使用独立 DMA 页，可传输连续扇区，短暂轮询后才让出 CPU；提交与完成由锁保护。fs 的 BlockDeviceClient 用同步批量请求代替逐扇区传递完成端点，IPC 缓冲放在受锁保护的实例内，避免挤占启动线程栈。多设备采用独立 MMIO/DMA 区域。当前不包括 AHCI/NVMe、MBR 扩展分区链或热插拔。

OS 本体默认使用 `make mkimg run` 从 VirtIO 硬盘启动，[storage 回归](../../../tests/storage/README.md) 使用隔离镜像。InfoIDE 项目默认从项目内的 osinfo.img 启动；仅选择 --boot cdrom 时，才使用上述可选 rootfs 内存模块路径。

## owner、权限与失败处理

device 在 pci 就绪后实际创建 storage，并保存它的唯一监护句柄；storage 本身依赖 pci 与 task。注册 storage 后继续设备初始化，device 的就绪点还包括驱动匹配完成。

Storage 角色加 Device 能力允许访问特定 BAR/DMA/IRQ 和引导 rootfs，普通 root 不拥有这些能力。RPC 的设备号、LBA、字节数、完成端点均逐项校验；接收到的端点/返回对象按请求寿命释放。分区节点只限定其范围，不能通过相对 LBA 访问相邻分区。

主请求循环与 IRQ 完成线程分开；DMA 请求槽、完成队列和设备对象有各自同步，不能持有队列锁阻塞等待用户通知。普通信号不能杀 storage，基础服务当前也不允许 systemctl 单独停止/重启。内核硬件机制不是 storage.kernel 进程。

驱动使用同一单调时钟预算等待空槽与硬件完成。提交前过期直接返回，提交后超时或非法完成使旧队列离线。停机和句柄关闭在队列锁外进行。SDK 同步传输和刷新有单次 IPC 截止时间，调用方超时不改变已经进入处理的设备操作，也不会触发自动重放。服务重新启动后，客户端应重新枚举并核对设备身份再发新请求。

设备资源域已提供 DMA 安全停机兜底：正常错误处理与服务退出都先确认 reset，失败保留 DMA 页并隔离设备。新实例先处理旧域，再建立新队列。具体机制见 [device.kernel](../device.kernel/README.md)。

未完成请求与外部写入结果仍需后续业务事务协议处理，重新注册不承诺写入重放或磁盘一致性。排查依次看 PCI 授权、设备协商、域停止/隔离日志、IRQ 确认、分区范围、FS 格式探测；不要把启动 rootfs 内存块当作持久磁盘。
