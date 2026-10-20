# device.kernel：受限设备资源机制

实现 `src/kernel/device/service/service.cc`，Ring0 驻留 EndpointService；`src/kernel/device/device.cc` 保存 MMIO、DMA、IRQ 对象。init 发布 `device.kernel`，没有独立服务进程或 systemctl stop 动作。

## 协议和授权

`os::DeviceOperation` 使用现有 IPC syscall。普通 root 身份不能访问下列设备机制。

| 操作 | 要求与结果 |
| --- | --- |
| Framebuffer | Display 能力；校验映射地址后返回 DisplayInfo |
| InitializeInput / ReadInput | Device 角色及 Device 能力；仅初始化 PS/2 或取下一字节及来源 |
| MapMmio | Device/Storage 角色与能力；指定实际 PCI 功能、BAR、范围，返回 MMIO 句柄 |
| AllocateDma | 设备角色及能力；返回有边界的 DMA 缓冲和身份 |
| BindIrq / AcknowledgeIrq | IRQ 范围、触发方式、通知端点能力；确认要求绑定对象的 Manage 权限 |
| BootImageInfo/Read/Write | 固定 Storage 角色与 Device 能力；只能访问保留的 rootfs 引导模块 |

OS 公共头不含 CS 检查、in/out 指令或任意端口代理。PS/2 端口访问在 `src/kernel/device/ps2.cc`，PCI 配置访问在 [pci.kernel](../pci/pci.kernel/README.md)。

## 中断、对象和锁

IRQ 绑定持有通知端点引用，硬件中断只发有界通知；级别触发设备完成处理后由驱动确认再解除屏蔽。绑定表使用中断安全锁，MMIO 地址空间修改使用地址空间锁。句柄销毁释放对象、解绑或取消映射，不能因进程退出残留有效设备权限。

存储 rootfs 内存块的实现为 `src/kernel/device/storage/boot_image.cc`。它是机制代码，不虚构 storage.kernel 进程、启动项或注册端点。映射统一复用既有 syscall。

## 故障与排查

权限失败先检查内核 ServiceRole 和能力，不能靠修改 runtime 配置增加能力。MMIO/DMA/IRQ 状态不因 Ring3 驱动重新注册自动恢复。观察 `[DEVICE]`、`[INPUT]`、`[STORAGE]`，区分驱动未确认 IRQ、订阅背压和真正的内核异常。

上层：[device](../README.md)、[storage](../storage/README.md)。权限与启动验证入口为 `tests/service-runtime.sh`，输入背压验证为 `tests/input-backpressure.sh`。
