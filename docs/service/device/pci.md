# pci 服务

`src/device/pci/main.cc` 是内置的Ring3 服务，启动后在注册表中发布 `pci` 端点。device 和 storage 服务通过 IPC 请求设备枚举、配置空间读取和写入。内核 `PciAccess` 负责访问 PCI 配置端口，端口操作不会进入用户 ABI。

内核只授予 pci 服务 `taskCapabilityPci`。`pci.kernel` 是该服务通过已有 IPC syscall 访问的驻留端点，内核同时检查可信系统服务身份和 PCI 能力。pci 服务根据内核标记的 `trustedDeviceService` 字段核验 IPC 发送者，拒绝普通进程，包括 root。注册表不允许覆盖已注册的 `pci` 端点。

请求类型为 `PciServiceRequest::{Enumerate, ReadConfig, WriteConfig}`。枚举最多返回 64 个 `PciFunctionInfo`；配置访问仅支持有效功能号和对齐的 32 位偏移。MMIO BAR、DMA 和 IRQ 资源仍由内核管理，并仅授予可信 device/storage 服务。

Ring0 端点位于 `src/kernel/device/pci/service.cc`，硬件配置访问位于同目录 `access.cc`；与 Ring3 PCI 服务分别实现，不存在 PCI 专用 syscall。

## 启动、协议布局和寿命

device 实际创建并监护 pci，持有唯一管理任务句柄；pci 注册端点后才启动 storage。固定 Pci 角色与 PCI 能力来自内置引导元数据，普通程序无权覆盖保护注册名。

Enumerate 的 data[0] 为容量，回复状态、数量和 PciFunctionInfo 数组（从 data[2] 开始）。Read/WriteConfig 的 data[0] 是偏移，data[1..] 是功能描述，写值为 data[3]；回复状态及读取值。参数和实际硬件范围在 pci.kernel 再次验证，客户端不能把任意物理地址作为 PCI 功能。

主线程串行 RPC，没有私有工作线程或长期持有客户端映像。调用结束释放收到的临时能力；配置访问锁在内核硬件访问层。基础 pci 当前不开放 systemctl stop/restart/disable；storage/device 依赖其发现与资源身份。

故障后旧端点失效，注册表清理发布者；新端点不表示设备先前配置或 DMA 状态重建。诊断先核对 systemctl status pci 的 owner=device，再查 PPid、ServiceRole、`[PCI]`、`[DEVICE]`。测试为 tests/service-runtime.sh；机制见 [pci.kernel](pci.kernel/README.md)。
