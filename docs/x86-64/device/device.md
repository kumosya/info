# 设备管理（src/kernel/device/）

本文档描述 `src/kernel/device/device.cc` 的实现：内核侧的设备访问控制层，为可信 pci/device/storage 服务及显示服务提供受能力门控的硬件资源。对应头文件 `include/kernel/device.h`。

内核负责硬件访问控制；可信 device/storage 服务在 Ring 0 运行。PCI 配置、MMIO、DMA、中断通过内核接口管理，端口指令由可信服务直接执行。所有类和函数位于 `namespace kernel::device`。

## 1. 初始化

```cpp
namespace kernel::device {
void init(const BootInfo* bootInfo);
}
```

从 `bootInfo->framebuffer` 复制启动帧缓冲信息到内核（供 `MapFramebuffer` 使用），并打印 `[DISPLAY]` 参数日志。同时注册 IRQ 处理函数 `irqHandler`（向量 64-79）。

## 2. I/O 端口访问

可信 device 服务通过 `os::SystemService` 在 Ring 0 执行端口指令。用户态不再拥有端口访问系统调用。

## 3. 帧缓冲映射（DisplayAccess）

```cpp
class DisplayAccess {
   public:
    static bool mapFramebuffer(Task& task, uint64_t userAddress, os::DisplayInfo& info);
};
```

校验链：

1. 进程须有 `taskCapabilityDisplay`；
2. 启动帧缓冲的 address/width/height/pitch 非零、`bitsPerPixel >= 24`；
3. `userAddress` 页对齐且 `>= 0x10000`；
4. 可见大小 = `pitch * height`，非零、≤ 64 MiB、无溢出；
5. 用户区间不回绕且 `< 2^47`。

映射：逐页 `memory::mapPageInAddressSpace`，页标志 `pageUser | pageWritable | pageNoExecute | pageCacheDisable | pageExternal`（用户可读写、不可执行、禁缓存、外部管理）。失败回滚。

填充 `os::DisplayInfo`：address（含页内偏移）、size、width、height、pitch、bitsPerPixel、颜色分量位置；像素格式判定：32bpp 且 R=16,G=8,B=0 → `Bgra`；R=0,G=8,B=16 → `Rgba`；其他 → `Unknown`。

对应系统调用 MapFramebuffer（60）。console 与 guiw 服务使用。

## 4. PCI 访问（PciAccess）

```cpp
class PciAccess {
   public:
    static uint32_t enumerate(os::PciFunctionInfo* functions, uint32_t capacity);
    static bool read(const os::PciFunctionInfo& function, uint16_t offset,
                     uint32_t& value);
    static bool write(const os::PciFunctionInfo& function, uint16_t offset,
                      uint32_t value);
    static bool getMemoryBar(const os::PciFunctionInfo& function,
                             uint8_t barIndex, uint64_t& base, uint64_t& size);
};
```

通过传统配置机制（端口 `0xcf8` 地址 / `0xcfc` 数据，32 位访问），全局 `pciLock` 保护。

- **枚举**：遍历 256 总线 × 32 设备；先探测 function 0 的 vendor ID（`0xffff` 为不存在）；读偏移 0x0c 的多功能位决定扫描 1 或 8 个 function；填充 `PciFunctionInfo`（vendorId、deviceId、classCode、subclass、programmingInterface、headerType）。受 `capacity` 限制。
- **读/写**：校验 `segment == 0 && device < 32 && function < 8`、`offset < 256`、4 字节对齐。
- **BAR 探测**（`getMemoryBar`）：保存原值；I/O BAR（bit 0）返回 false；临时关闭命令寄存器内存/IO 解码，向 BAR 写全 1 读回掩码，恢复原值；输出 base 与 `size = ~mask + 1`；支持 64 位内存 BAR。

独立的 `pci` 服务通过 `CallPciService`（4111）向内核 `PciAccess` 提交枚举、配置读取和配置写入请求；只有该服务持有 PCI 能力。device/storage 通过 IPC 调用 pci 服务。端口指令只存在于内核 PCI 驱动中。

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
   public:
    static MmioRegion* map(Task& task, const os::PciFunctionInfo& function,
                           uint8_t barIndex, uint64_t userAddress, uint64_t size);
   private:
    uint64_t physicalAddress_;
    uint64_t virtualAddress_;
    uint64_t size_;
};
```

校验用户地址/大小页对齐、无溢出、在用户半区；`getMemoryBar` 且 `size <= barSize`、BAR 物理基址页对齐；逐页映射（标志同帧缓冲）；失败回滚。成功返回对象（作为 `HandleType::MmioRegion` 句柄）。

对应系统调用 MapDeviceMmio（4114）。storage 服务用它映射 virtio PCI 能力区域。

## 6. DMA 缓冲（DmaBuffer）

```cpp
class DmaBuffer {
   public:
    static DmaBuffer* create(Task& task, uint64_t userAddress,
                             os::DmaBufferInfo& info);
   private:
    uint64_t physicalAddress_;
    uint64_t virtualAddress_;
    uint64_t size_;
};
```

分配**一个物理页**并映射到用户地址（`pageUser | pageWritable | pageNoExecute`）；填充 `DmaBufferInfo`：`virtualAddress = userAddress`、`deviceAddress = 物理地址`、`size = 4096`（固定单页）。

对应系统调用 AllocateDma（4115）。storage 服务用它构建 virtqueue。

## 7. 中断绑定（InterruptBinding）

```cpp
class InterruptBinding {
   public:
    static InterruptBinding* create(Task& task, uint8_t irq,
                                    Endpoint* endpoint,
                                    bool levelTriggered = false);
    uint8_t irq() const;
    Endpoint* endpoint() const;
    bool requiresAcknowledgement() const;
    bool acknowledge(Task& task) const;
   private:
    uint8_t irq_;
    Endpoint* endpoint_;
    Task* owner_;
    uint32_t apicId_;
    bool levelTriggered_;
};
```

- 校验 `irq <= 15` 且 endpoint 非空；每个 IRQ 只能绑定一次（`bindings[16]` 表）；
- `interrupt::registerHandler(64 + irq, irqHandler)`；
- `interrupt::setIrq(irq, 64 + irq, cpu::getCurrent()->getApicId(), levelTriggered)` 配置 I/O APIC 路由（见 [cpu.md](cpu/cpu.md)）。

中断触发时 `irqHandler` 校验向量在 `[64, 79]`：
- 电平触发中断先屏蔽 IRQ（`interrupt::maskIrq`），用户态通过 AcknowledgeIrq（4167）系统调用重新启用；
- 调用 `endpoint->notifyFromInterrupt(0x495251, irq)`——以消息类型 `0x495251`（ASCII "IRQ"）向绑定的 IPC 端点投递通知（见 [ipc.md](task/ipc.md)）。

对应系统调用 BindIrq（20）、AcknowledgeIrq（4167）。device 服务绑定键盘（IRQ 1）与鼠标（IRQ 12），storage 服务绑定 virtio-blk 中断。

## 8. 接口汇总

| 接口               | 所需能力    | 系统调用       | 使用者                          |
| ---------------- | ------- | ---------- | ---------------------------- |
| DisplayAccess    | Display | 60       | console、guiw               |
| PciAccess        | Pci     | 4111       | pci-server                    |
| MmioRegion       | Device  | 4114       | storage-server（virtio）       |
| DmaBuffer        | Device  | 4115       | storage-server（virtqueue）    |
| InterruptBinding | Device  | 4116       | device-server、storage-server |
