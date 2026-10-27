# 设备资源与安全回收

设备资源实现位于 `src/kernel/device/resource/`，声明在 `include/kernel/device.h`，使用 `kernel::device`。domain.cc 管理设备资源域与停机，mmio.cc 建立 BAR 映射，irq.cc 管理中断绑定。DMA 分配见[DMA 缓冲区](dmabuffer.md)。

## 资源域保存什么

ResourceDomain 对应一个被认领的 PCI 功能，保存所有者 Task、功能身份、引用数、DMA 页数、地址空间代际、BAR 边界、内核控制映射，以及 MMIO/DMA 对象链。exiting_ 表示所有者正在退出，addressSpaceGone_ 表示用户地址空间已释放。

ResourceDomains 保存 32 个域槽、注册表锁和回收线程。`resourceDomains()` 返回共用注册表。每个域最多登记 2048 个 DMA 页；槽位和页数都是有界资源。

| 状态 | 含义 |
| --- | --- |
| Free | 槽位可认领。 |
| Active | 允许建立新资源并进行受限配置。 |
| Pending | 已请求停止，等待工作线程。 |
| Stopping | 工作线程正在撤销访问并复位设备。 |
| Stopped | 已确认设备停止，可在其他引用释放后回收。 |
| Quarantined | 无法确认设备停稳，保留 DMA 页和域，禁止当作空闲资源复用。 |

## 认领与配置写入

`ResourceDomains::claim(owner, function)` 要求 owner 具有 Storage 角色和 Device 能力。function 确定 PCI 位置；函数读取实际设备身份，不直接相信请求附带的 vendor/device 信息。

当前可信复位流程只支持现代 VirtIO block，身份值为 0x10421af4。claim 查找空槽并拒绝重复认领，关闭 bus mastering，探测 BAR，遍历 VirtIO capability，定位 common configuration 内的 device_status。然后建立内核专用控制映射，保存 BAR 范围与地址空间代际，保留 owner 引用，最后发布 Active。没有可信复位入口的设备不能套用这套 DMA 回收流程。

认领过程中失败不会发布域。前面已经关闭的 bus mastering 不保证恢复为原值；不能把失败理解为硬件配置完全未变。对退出中且处于 Quarantined 的旧域，新的认领尝试只会触发再次停机处理，本次仍返回失败。

`writeConfiguration(owner, function, offset, value)` 是 PCI 代理写配置的约束入口。已有域时，只允许匹配所有者、Active 状态、偏移 4 的 command 写入；BAR 与 capability 不能被随意改动。没有域时，也只允许 command 写入，并要求 bus mastering 位清零。实际只写 value 的低 16 位。

## 资源注册和引用

| 函数 | 作用与参数 |
| --- | --- |
| `ResourceDomains::init()` | 创建资源回收线程；无法创建时停止启动。 |
| `owned(owner)` | 查询是否仍有归属于 owner 的域，包括尚未回收的停止或隔离状态。 |
| `statistics()` | 统计活动、待处理、停止、隔离域及 DMA 页数。 |
| `find(owner, function)` | 查找该所有者的 Active 域并增加临时引用；调用者必须平衡引用。 |
| `ResourceDomain::active(owner)` | 检查所有者和 Active 状态。 |
| `attach(region)` | 将 MMIO 包装对象挂入域链并增加引用；非 Active 拒绝。 |
| `attach(buffer)` | 挂入 DMA 对象、增加引用和页数；同时检查总页数限额。 |
| `allowsIrq(owner, irq)` | 检查活动所有者，并读取 PCI 中断线字段与 irq 比较。 |
| `closeLease()` | 关闭域租约，Active 转 Pending，减少引用并唤醒工作线程。 |
| `drop()` | 释放临时或子资源引用，唤醒回收线程。 |

Statistics 的 getter 分别涉及活动域数、待处理域数、停止域数、隔离域数和 DMA 页数。Pending 统计包含 Stopping。

域链拥有子资源包装对象的最终回收责任。关闭 MMIO 或 DMA 句柄只撤销用户映射、减少域引用，不直接从域链释放包装对象。这样工作线程仍能找到并处理所有硬件可达资源。

## 停机、退出和隔离

`stop(owner)` 验证所有者，提交 Pending 后等待，期间不持注册表锁。Stopped 返回零，Quarantined 返回 -11，等待超过两秒返回 -9，无权调用返回 -3。超时表示本次等待结束，不代表后台停机已经取消。

`ownerExiting(owner)` 标记退出，将 Active 域转 Pending；`addressSpaceReleased(owner)` 登记页表已释放。两者解决不同条件：CPU 不再执行该任务，并不证明设备停止 DMA；设备停止也不证明旧页表及句柄都已销毁。

私有 `reset()` 先撤销用户 MMIO 映射，再清除 PCI bus mastering，向 device_status 写零并执行 mfence。它最多等待 100 ms，随后再次确认 bus mastering 未开启。这样既切断驱动继续编程设备的路径，也确认设备不再访问旧 DMA 地址。失败转入 Quarantined，保留页面；不能靠强制释放掩盖失败。

`work()` 在锁内选择 Pending 域并标记 Stopping，在锁外执行 reset，之后发布结果。只有 Stopped、引用数为零，而且退出中的所有者已完成地址空间释放，才回收 DMA 物理块、DMA/MMIO 包装对象和内核控制映射，清空槽位，最后释放 owner 引用。它在没有工作时按登记等待后再阻塞的顺序休眠，避免丢失唤醒。

## MMIO 映射

`MmioRegion::map(task, function, barIndex, userAddress, size, offset)` 查找活动域，在已记录的 BAR 内建立映射。barIndex 为 0～5；size 非零且按页对齐；offset 必须页对齐且不超出 BAR。userAddress 为零时自动选择，否则要求页对齐并处于用户范围。失败返回空指针。

函数持地址空间锁逐页映射，使用用户可写、不可执行、禁用缓存及 external 标志。任一映射、包装对象分配或 attach 失败，都撤销本次映射；find 取得的临时引用在返回前释放。

对象的 `address()`、`size()` 对应用户基址和映射长度。`destroy()` 原子地执行一次关闭，调用域的 `unmap(address, size, mapped, physical)` 撤销映射，再 drop 引用。

unmap 同时核对 mapped 标志、地址空间代际及每页实际物理地址。exec 之后即使虚拟地址相同，也不能撤销新映像的页面。撤销 external 映射不释放设备 BAR 的物理存储。

## IRQ 到端点通知

InterruptBinding 保存 IRQ、端点、所有者、目标 APIC ID 和触发方式。irq.cc 的 bindings[16] 由 bindingLock 保护，每条 IRQ 只允许一个绑定。

`create(task, irq, endpoint, levelTriggered)` 校验 IRQ 范围和端点，在向量 64 + irq 上登记 handler，配置 I/O APIC 路由到当前 CPU，保留端点引用并发布绑定。登记或路由失败会撤销已建立的部分；角色和域授权由外层 device.kernel 检查。

`irqHandler(context)` 根据中断向量定位绑定。电平触发时先屏蔽 IRQ，再调用端点的 `notifyFromInterrupt(0x495251, irq)` 发出通知，避免驱动尚未清除设备状态就反复进入中断。

`acknowledge(task)` 要求同一所有者；电平触发时重新配置路由开放 IRQ。驱动应先清除设备的中断原因，再发送确认；确认本身不代替设备寄存器操作。边沿触发不需要同样的重新开放步骤。

`destroy()` 在绑定锁内屏蔽 IRQ、移除表项并注销 handler，离开锁后释放端点引用与包装对象。`irq()`、`endpoint()`、`requiresAcknowledgement()` 分别涉及 IRQ 号、端点及电平确认标志。

这条路径是传统 IRQ 0～15 的绑定机制，没有实现通用 MSI/MSI-X 管理。端点通知和中断控制器细节见[IPC](../task/ipc.md)与[中断](../cpu/interrupt.md)。
