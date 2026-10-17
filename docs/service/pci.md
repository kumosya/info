# pci 服务

`src/pci/main.cc` 是内置的可信 Ring 0 服务，启动后在注册表中发布 `pci` 端点。device 和 storage 服务通过 IPC 请求设备枚举、配置空间读取和写入。内核 `PciAccess` 负责访问 PCI 配置端口，端口操作不会进入用户 ABI。

内核只授予 pci 服务 `taskCapabilityPci`。`CallPciService` 是该服务向内核提交请求的单一入口，内核同时检查可信系统服务身份和 PCI 能力。pci 服务根据内核标记的 `trustedDeviceService` 字段核验 IPC 发送者，拒绝普通进程，包括 root。注册表不允许覆盖已注册的 `pci` 端点。

请求类型为 `PciServiceRequest::{Enumerate, ReadConfig, WriteConfig}`。枚举最多返回 64 个 `PciFunctionInfo`；配置访问仅支持有效功能号和对齐的 32 位偏移。MMIO BAR、DMA 和 IRQ 资源仍由内核管理，并仅授予可信 device/storage 服务。
