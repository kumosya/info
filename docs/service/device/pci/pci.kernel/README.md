# pci.kernel：PCI 配置空间机制

独立实现为 `src/kernel/device/pci/service.cc`，硬件访问为同目录 `access.cc`。这是 Ring0 驻留对象，发布名 `pci.kernel`；Ring3 [pci 服务](../README.md)为独立镜像，二者不可混为一个进程。

## 协议与能力

`os::PciOperation` 包含 Enumerate、ReadConfig、WriteConfig。枚举传入容量；配置访问传 PciFunctionInfo、偏移和写值。结果包含原生状态、枚举记录或 32 位配置值。

只允许持 PCI 能力且具有固定 Pci 角色的调用者。普通应用和 root 不能通过自行构造请求或同名二进制获得访问权。Ring3 pci 对 device/storage 的认证是额外一层，不替代这里的内核验证。

## 机制范围与同步

配置访问验证总线、设备、功能号和对齐偏移。配置地址/数据端口的成对操作必须串行，硬件 I/O 保留在内核实现中。枚举结果不是 MMIO/DMA 能力；BAR 映射、DMA 和 IRQ 仍由 device.kernel 对实际资源另行授权。

没有新增 PCI 专用 syscall，没有任意端口读写 RPC，没有独立工作线程或客户端资源缓存。请求在调用上下文同步完成；能力句柄与 Ring3 发现端点遵循通用句柄所有权。

## 生命周期与诊断

对象随内核驻留，systemctl 管理的是 Ring3 pci，而非此对象。未知操作、非法参数、权限失败明确返回错误；不能用重试掩盖无权限调用。Ring3 驱动失败不表示配置机制失效。

诊断顺序：内核固定角色 → pci 服务就绪 → device 枚举和驱动匹配 → storage I/O。真实父子关系由 `tests/service-runtime.cc` 检查；硬件机制与设备范围参阅 [device.kernel](../../device.kernel/README.md)。
