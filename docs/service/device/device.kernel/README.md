# device.kernel：受限设备资源机制

实现 `src/kernel/device/service/service.cc`，Ring0 驻留 EndpointService；`src/kernel/device/device.cc` 保存 MMIO、DMA、IRQ 对象。init 发布 `device.kernel`，没有独立服务进程或 systemctl stop 动作。

## 协议和授权

`os::DeviceOperation` 使用现有 IPC syscall。普通 root 身份不能访问下列设备机制。

| 操作 | 要求与结果 |
| --- | --- |
| Framebuffer | Display 能力；校验映射地址后返回 DisplayInfo |
| InitializeInput / ReadInput | Device 角色及 Device 能力；仅初始化 PS/2 或取下一字节及来源 |
| ClaimDevice | 固定 Storage 角色与 Device 能力；指定现代 VirtIO block 功能，返回设备资源域句柄 |
| StopDevice | 设备域拥有者及 Manage 权限；有界等待可信 reset 确认，失败保页并返回错误 |
| MapMmio | 已 claim 的设备拥有者；指定实际 PCI 功能、BAR、范围，返回关联资源域的 MMIO 句柄 |
| AllocateDma | 设备域拥有者；传资源域句柄和目标虚拟地址，返回一页固定 DMA 缓冲及物理身份 |
| BindIrq / AcknowledgeIrq | IRQ 范围、触发方式、通知端点能力；确认要求绑定对象的 Manage 权限 |
| BootImageInfo/Read/Write | 固定 Storage 角色与 Device 能力；只能访问保留的 rootfs 引导模块 |

OS 公共头不含 CS 检查、in/out 指令或任意端口代理。PS/2 端口访问在 `src/kernel/device/ps2.cc`，PCI 配置访问在 [pci.kernel](../pci/pci.kernel/README.md)。

## 中断、对象和锁

IRQ 绑定持有通知端点引用，硬件中断只发有界通知；级别触发设备完成处理后由驱动确认再解除屏蔽。绑定表使用中断安全锁，MMIO 地址空间修改使用地址空间锁。句柄销毁释放对象、解绑或取消映射。

存储 rootfs 内存块的实现为 `src/kernel/device/storage/boot_image.cc`。它是机制代码，不虚构 storage.kernel 进程、启动项或注册端点。映射统一复用既有 syscall。

## 故障与排查

权限失败先检查内核 ServiceRole 和能力。现代 VirtIO block 必须 claim 设备资源域，DMA 分配关联域，MMIO 使用关闭 DMA 时探测并缓存的 BAR 范围；PCI 代理传递真实 requester，禁止无 owner 开启总线主控。

域独立持有 DMA 物理页，进程地址空间销毁不会自动释放它们。独立回收线程撤销 MMIO、关闭 bus master、写 reset 并确认完成；句柄与旧地址空间均释放后才回收页。失败隔离设备并保留页，后续成功 reset 才能重新 claim；不阻塞全局任务回收。设备映射禁止通过 fork 逃出 owner，exec 清理校验地址空间 generation。IRQ 在 owner 句柄回收时解绑。

上述机制不等于 storage 在途写入和 FS 事务已恢复。观察 `[DEVICE] DMA domain stopped/reclaimed/quarantined`、`[INPUT]`、`[STORAGE]`；测试 `tests/device-resources.sh` 覆盖重复服务退出、reset 失败保页及再次恢复。

上层：[device](../README.md)、[storage](../storage/README.md)。权限与启动验证入口为 `tests/service-runtime.sh`，输入背压验证为 `tests/input-backpressure.sh`。
