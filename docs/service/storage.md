# storage 服务（src/storage/）

storage 服务（注册名 `"storage"`）实现 **VirtIO Block（virtio-blk）PCI 现代（1.0+）驱动**、MBR/GPT 分区解析与同步/异步块 I/O。由内核内置镜像加载，授予 `processCapabilityDevice`。

## 1. 设备模型

协议头 `os::BlockDeviceType` 预留 `VirtioBlock=1, Ahci, Nvme, UsbStorage`，但**实际驱动仅实现 VirtIO Block**（PCI vendor `0x1af4`、device `0x1042`）。

设备容量上限 `deviceCapacity = 2`：

- **设备 0**：第一块 virtio-blk 盘的**第一个分区**（MBR 或 GPT 解析），作为根文件系统；
- **设备 1**：若存在第二块盘，则为整盘 `{0, blockCount, 0}`，作为 cdrom 介质。

## 2. 关键常量

| 常量 | 值 | 说明 |
|---|---|---|
| `blockSize` | 512 | 扇区大小 |
| `queueSize` | 32 | virtqueue 深度 |
| `requestSlotCount` | 7 | 并发请求槽 |
| `requestStride` | 544 | 16 字节头 + 512 数据 + 1 状态 + 对齐 |
| `ioTimeoutNanoseconds` | 2 s | I/O 超时 |
| DMA 队列区 | `0x30000000` | virtqueue 描述符/available/used |
| DMA 请求区 | `0x30001000`（每设备 +0x2000） | 请求缓冲 |
| MMIO 基址 | `0x40000000` | 每 BAR 0x10000、每设备 0x100000 |

## 3. VirtIO PCI 传输层

`VirtioPciTransport`：

- 遍历 PCI 能力链表（cap pointer @0x34，最多 48 步），识别 `VIRTIO_PCI_CAP_ID = 0x09`，按 cfg_type 收集 common(1)/notify(2)/isr(3)/device(4) 四类区域的 BAR/偏移/长度；notify 能力还读取 `notify_off_multiplier`；区域 ≤ 64 KiB。
- 经 `os::mapDeviceMmio` 映射所需 BAR；使能 PCI 命令寄存器 0x0006（内存空间 + 总线主控）。

## 4. 驱动初始化（标准 virtio 1.0 握手）

1. 状态复位 → ACKNOWLEDGE | DRIVER；
2. 读设备特性（sel 0/1）；**强制要求 `VIRTIO_F_VERSION_1`（bit 32）**；
3. 协商特性 = VERSION_1 | READONLY(bit 5) | FLUSH(bit 9)，写回并验证 FeaturesOk；
4. 队列设置：选队列 0，查最大尺寸（≥32），写大小；描述符表/available/used 三段布局在同一 4 KiB DMA 页内（`availableOffset = 16*32 = 512`，used 4 字节对齐）；写三个 64 位物理地址（common 偏移 32/40/48），读 notify_off（偏移 30），置 queue_enable；
5. 读设备配置 `capacity`（8 字节 @0）；
6. 绑定中断（要求 IRQ ≤ 15）；
7. DRIVER_OK。

## 5. I/O 提交

```cpp
struct VirtioBlockRequest { uint32_t type; uint32_t reserved; uint64_t sector; };  // 16 字节
```

类型：0=读、1=写、4=FLUSH。

- 每请求 3 个描述符：头（设备读）→ 数据 512 字节（读请求为 WRITE 标志）→ 1 字节状态（设备写，预置 0xFF）；FLUSH 只有头 + 状态 2 个描述符。
- 提交后更新 available ring（release 屏障）并 notify；完成经 used ring（acquire 屏障）回收。
- 2 秒超时 → 标记设备离线并置 FAILED 状态。
- 中断线程读 ISR 寄存器确认中断后处理 used ring。

## 6. 分区解析（`GptParser::findFirstPartition`）

1. 读 LBA0：校验 `0x55AA`；扫描 4 个 MBR 表项（偏移 446+16i），取第一个类型非 0、非 0xEE、范围合法的分区 `{start, count, type}`。
2. 无 MBR 分区则读 LBA1 验证 GPT：签名 `"EFI PART"`、头大小 92-512、头 CRC32（多项式 0xEDB88320）、表项数 ≤128、表项大小 128-512 且整除 512、first/last usable 合法；逐项累计表项 CRC32，取第一个非空 GUID 分区 `{start, end-start+1, type=1}`，且范围须落在 usable 区间内。

## 7. IPC 请求（`os::StorageRequest`，基值 0x100）

块寻址编码：`data[0] = (deviceId << 56) | (block & ((1<<56)-1))`（`encodeStorageBlock`）。

| 请求 | 值 | 语义 |
|---|---|---|
| GetInfo | 0x100 | 回复 blockCount、512、maxTransfer=1、type=VirtioBlock、readOnly |
| ReadBlocks | 0x101 | `data[1]`=块内偏移、`data[2]`=大小；读上限 4104 字节 |
| WriteBlocks | 0x102 | 写上限 4096 字节；支持子块读-改-写；只读设备返回 ReadOnly |
| Flush | 0x103 | 协商了 FLUSH 特性则提交 type=4 请求，否则直接成功 |
| ReadAsync | 0x104 | `handles[0]`=完成端点、`data[1]`=requestId |
| WriteAsync | 0x105 | `data[2]`=512，数据占满 `data[3..]` |

异步完成消息（类型 `0x53544f52414745` "STORAGE"）：`data[0]`=requestId、`data[1]`=错误码、`data[2]`=传输字节数、读成功时 `data[3..]`=512 字节数据；随后关闭端点。

错误码 `os::StorageError`：Success=0、DeviceNotFound=-1、DeviceOffline=-2、InvalidBlock=-3、Io=-4、Timeout=-5、ReadOnly=-6、NoSpace=-7、Dma=-8、Corrupted=-9、InvalidArgument=-10。

## 8. 线程模型

1 个 IRQ 线程 + 3 个工作线程 + 主线程，共 5 个线程并发处理请求（内部用自旋锁保护请求槽）。栈基址 `0x30010000`（IRQ）与 `0x30020000`（3 个 64 KiB 工作栈）。
