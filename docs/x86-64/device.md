# 设备子系统（src/kernel/device/）

本文档描述 `src/kernel/device/device.cc` 的实现：内核侧的设备访问控制层，为用户态驱动（device-server、storage-server、console、window）提供受能力门控的硬件通道。对应头文件 `include/kernel/device.h`。

内核本身不实现具体设备驱动；PCI 配置、I/O 端口、MMIO、DMA、中断都由用户态服务通过这些接口访问。

## 1. 初始化

```cpp
void initDeviceSubsystem(const BootInfo* bootInfo);
```

从 `bootInfo->framebuffer` 复制启动帧缓冲信息到内核（供 `MapFramebuffer` 使用），并打印 `[DISPLAY]` 参数日志。

## 2. I/O 端口访问（PortAccess）

```cpp
class PortAccess {
    static bool read8(const Process& process, uint16_t port, uint8_t& value);
    static bool write8(const Process& process, uint16_t port, uint8_t value);
};
```

- 要求进程持有 `processCapabilityDevice` 能力；
- **仅允许 PS/2 控制器端口** **`0x60`** **与** **`0x64`**（键盘/鼠标）；
- 通过后执行 `inb`/`outb`。

这是用户态键盘/鼠标驱动的唯一端口通道。对应系统调用 ReadIoPort（27）/WriteIoPort（28）。

## 3. 帧缓冲映射（DisplayAccess）

```cpp
class DisplayAccess {
    static bool mapFramebuffer(Process& process, uint64_t userAddress, os::DisplayInfo& info);
};
```

校验链：

1. 进程须有 `processCapabilityDisplay`；
2. 启动帧缓冲的 address/width/height/pitch 非零、`bitsPerPixel >= 24`；
3. `userAddress` 页对齐且 `>= 0x10000`；
4. 可见大小 = `pitch * height`，非零、≤ 64 MiB、无溢出；
5. 用户区间不回绕且 `< 2^47`。

映射：逐页 `mapPageInAddressSpace`，页标志 `pageUser | pageWritable | pageNoExecute | pageCacheDisable | pageExternal`（用户可读写、不可执行、禁缓存、外部管理）。失败回滚。

填充 `os::DisplayInfo`：address（含页内偏移）、size、width、height、pitch、bitsPerPixel、颜色分量位置；像素格式判定：32bpp 且 R=16,G=8,B=0 → `Bgra`；R=0,G=8,B=16 → `Rgba`；其他 → `Unknown`。

对应系统调用 MapFramebuffer（29）。console 与 window 服务使用。

## 4. PCI 访问（PciAccess）

```cpp
class PciAccess {
    static uint32_t enumerate(os::PciFunctionInfo* functions, uint32_t capacity);
    static bool read(const os::PciFunctionInfo& function, uint16_t offset, uint32_t& value);
    static bool write(const os::PciFunctionInfo& function, uint16_t offset, uint32_t value);
    static bool getMemoryBar(const os::PciFunctionInfo& function, uint8_t barIndex,
                             uint64_t& base, uint64_t& size);
};
```

通过传统配置机制（端口 `0xcf8` 地址 / `0xcfc` 数据，32 位访问），全局 `pciLock` 保护。

- **枚举**：遍历 256 总线 × 32 设备；先探测 function 0 的 vendor ID（`0xffff` 为不存在）；读偏移 0x0c 的多功能位决定扫描 1 或 8 个 function；填充 `PciFunctionInfo`（vendorId、deviceId、classCode、subclass、programmingInterface、headerType）。受 `capacity` 限制。
- **读/写**：校验 `segment == 0 && device < 32 && function < 8`、`offset < 256`、4 字节对齐。
- **BAR 探测**（`getMemoryBar`）：保存原值；I/O BAR（bit 0）返回 false；临时关闭命令寄存器内存/IO 解码，向 BAR 写全 1 读回掩码，恢复原值；输出 base 与 `size = ~mask + 1`；支持 64 位内存 BAR。

对应系统调用 EnumeratePci（15）、ReadPciConfig（16）、WritePciConfig（17）。

```cpp
struct PciFunctionInfo {   // os/abi.h
    uint16_t segment; uint8_t bus, device, function;
    uint8_t classCode, subclass, programmingInterface;
    uint16_t vendorId, deviceId; uint8_t headerType; uint8_t reserved[3];
};
```

## 5. MMIO 区域（MmioRegion）

```cpp
class MmioRegion {
    static MmioRegion* map(Process& process, const os::PciFunctionInfo& function,
                           uint8_t barIndex, uint64_t userAddress, uint64_t size);
};
```

校验用户地址/大小页对齐、无溢出、在用户半区；`getMemoryBar` 且 `size <= barSize`、BAR 物理基址页对齐；逐页映射（标志同帧缓冲）；失败回滚。成功返回对象（作为 `HandleType::MmioRegion` 句柄）。

对应系统调用 MapDeviceMmio（18）。storage 服务用它映射 virtio PCI 能力区域。

## 6. DMA 缓冲（DmaBuffer）

```cpp
class DmaBuffer {
    static DmaBuffer* create(Process& process, uint64_t userAddress, os::DmaBufferInfo& info);
};
```

分配**一个物理页**并映射到用户地址（`pageUser | pageWritable | pageNoExecute`）；填充 `DmaBufferInfo`：`virtualAddress = userAddress`、`deviceAddress = 物理地址`、`size = 4096`（固定单页）。

对应系统调用 AllocateDma（19）。storage 服务用它构建 virtqueue。

## 7. 中断绑定（InterruptBinding）

```cpp
class InterruptBinding {
    static InterruptBinding* create(Process& process, uint8_t irq, Endpoint* endpoint);
    uint8_t irq() const;
    Endpoint* endpoint() const;
};
```

- 校验 `irq <= 15` 且 endpoint 非空；每个 IRQ 只能绑定一次（`bindings[16]` 表）；
- `registerInterruptHandler(64 + irq, irqHandler)`；
- `configureIrq(irq, 64 + irq, getCurrentCpu()->apicId)` 配置 I/O APIC 路由（见 [interrupt.md](interrupt.md)）。

中断触发时 `irqHandler` 校验向量在 `[64, 79]`，调用 `notifyEndpointFromInterrupt(endpoint, 0x495251, irq)`——以消息类型 `0x495251`（ASCII "IRQ"）向绑定的 IPC 端点投递通知（见 [ipc.md](ipc.md)）。

对应系统调用 BindIrq（20）。device 服务绑定键盘（IRQ 1）与鼠标（IRQ 12），storage 服务绑定 virtio-blk 中断。

## 8. 接口汇总

| 接口               | 所需能力    | 系统调用     | 使用者                          |
| ---------------- | ------- | -------- | ---------------------------- |
| PortAccess       | Device  | 27/28    | device-server（PS/2）          |
| DisplayAccess    | Display | 29       | console、window               |
| PciAccess        | Device  | 15/16/17 | device-server、storage-server |
| MmioRegion       | Device  | 18       | storage-server（virtio）       |
| DmaBuffer        | Device  | 19       | storage-server（virtqueue）    |
| InterruptBinding | Device  | 20       | device-server、storage-server |

