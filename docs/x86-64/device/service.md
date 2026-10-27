# 设备机制服务

`src/kernel/device/service/service.cc` 在 `kernel::task` 中提供 `deviceKernelService()`，将内部 `DeviceService::call(caller, request, reply)` 绑定为 EndpointService。它运行在内核中，用户态通过 IPC 调用 device.kernel，而非直接执行端口指令或访问物理内存。

caller 是内核确定的调用任务；request 包含操作码、参数和句柄编号；reply 接收数据和新句柄。授权依据 caller 的真实角色、能力和句柄表，不能相信消息中自报的身份。

## 请求分派和权限

BootImageInfo、BootImageRead、BootImageWrite 首先单独分派，要求 Storage 角色与 Device 能力。其余请求要求至少五个数据字；Framebuffer 和 FrameInfo 在通用设备角色检查之前处理。

| 请求 | 参数和行为 | 授权 |
| --- | --- | --- |
| `Framebuffer` | data[0] 为指定地址，零表示自动选择。创建 FrameMapping，data[0] 返回句柄，后续数据返回 DisplayInfo。 | Display 能力。 |
| `FrameInfo` | data[0] 为 FrameMapping 句柄，查询仍有效的映射信息。 | 句柄 Read 权限及映射所有者检查。 |
| `InitializeInput` | 初始化 PS/2，data[0] 为鼠标包长度或初始化错误。 | Device 能力和 Device 角色。 |
| `ReadInput` | 读取一个原始字节，data[1] 为字节，data[2] 表示是否来自鼠标。 | Device 能力和 Device 角色。 |
| `MapMmio` | data[0] 为 BAR 编号，data[1] 为用户地址，data[2] 为长度，data[3] 起存 PCI 功能身份。创建 MMIO 句柄。 | Device 能力，Device 或 Storage 角色；还须有匹配且活动的资源域。 |
| `MapBar` | data[0] 为 BAR，data[1] 为长度，data[2] 为页对齐偏移，data[3] 起为 PCI 身份，data[5] 为资源域句柄。长度向上取整；返回句柄、地址和长度。 | 同上，且域句柄有 Manage 权限、属于调用者并处于 Active。 |
| `CreateDma` | data[0] 为大小，data[1] 为域句柄，data[2] 为对齐；自动选地址，返回 DmaBufferInfo。 | Device 能力，Device 或 Storage 角色，以及可管理的活动域。 |
| `AllocateDma` | data[0] 为用户地址，data[1] 为域句柄；默认分配一个 4 KiB 块。 | 同 CreateDma。 |
| `ClaimDevice` | data 起为 PCI 身份；调用资源域注册表认领设备，返回域句柄。 | 分派先检查设备角色及能力；实际 claim 进一步只接受 Storage 角色及受支持设备。 |
| `StopDevice` | data[0] 为域句柄，等待设备停机结果。 | 域句柄 Manage 权限及所有者检查。 |
| `BindIrq` | data[0] 为 IRQ，data[1] 为端点句柄，data[2] 为电平触发标志，data[3] 为域句柄。返回绑定句柄。 | 端点 Read 权限；Storage 须通过域的 IRQ 检查；Device 只允许 IRQ 1、12。 |
| `AcknowledgeIrq` | data[0] 为绑定句柄，处理电平中断后的重新开放。 | 绑定句柄 Manage 权限及所有者检查。 |
| `BootImageInfo` | 返回引导块设备容量。 | Storage 角色与 Device 能力。 |
| `BootImageRead`、`BootImageWrite` | 有界访问引导模块，消息布局见[存储](storage.md)。 | 同 BootImageInfo。 |

MapBar 至少需要六个数据字。BindIrq 将 IRQ 限制为 0～15，触发标志限制为 0 或 1。PCI 枚举和配置请求由独立的[PCI 服务](pci.md)处理。

## 从对象到句柄

创建操作先建立资源对象，再向调用者句柄表加入对应类型与权限。如果句柄分配失败，立即调用对象的销毁或租约关闭入口，避免留下用户无法关闭的资源。成功句柄设置 CloseOnExec，并通过 `addCreatedHandle(reply, handle)` 登记到回复句柄列表，交给 IPC 传递路径处理。

查询已有对象使用 HandleTable::Pin，在校验类型和权限的同时固定对象寿命。处理函数只在 pin 有效期间使用指针；不能取出裸指针后提前解除保护。

## 错误和回复

处理函数返回值表示调用是否成功：参数错误通常为 -1，句柄或对象状态无效为 -2，无权限为 -3，创建失败为 -5，无输入字节为 -6。StopDevice 保留资源域返回的超时或 I/O 错误。

回复数据也有自己的含义。InitializeInput 可在 data[0] 携带 PS/2 初始化错误；MapMmio 和 BindIrq 在部分创建失败路径中以零句柄表示失败。因此客户端必须同时解释调用状态和该操作的回复约定，不能把 data[0] 统一当作错误码。常规成功分支返回完整数据区长度；BootImage 分支自行设置实际长度。

映射校验、设备复位、DMA 页的延后释放分别见[设备机制](device.md)、[资源域](resource.md)和[DMA](dmabuffer.md)。
