# 设备机制

设备模块保存引导设备信息，并把硬件访问包装成有权限和生命周期的内核对象。声明主要在 `include/kernel/device.h`，命名空间为 `kernel::device`。PCI、时间和 ACPI 各有自己的头文件；时间和 ACPI 当前分别使用 `kernel::time`、`kernel::acpi`。

## 文件分工

以下路径均相对于 `src/kernel/device/`。

| 文件 | 作用 |
| --- | --- |
| `device.cc` | 保存引导帧缓冲，建立、查询和撤销用户帧缓冲映射。 |
| `serial.cc` | 初始化 COM1，输出串口诊断，并提供不等待输出锁的应急输出。 |
| `service/service.cc` | 检查 device.kernel 请求的角色、能力和句柄权限，创建资源对象。 |
| `pci/access.cc` | 串行访问 PCI 配置端口，枚举功能并探测内存 BAR。 |
| `pci/service.cc` | 实现 pci.kernel 请求，限制配置写入。 |
| `resource/domain.cc` | 管理设备所有者、MMIO/DMA 引用、停机和延后回收。 |
| `resource/mmio.cc` | 分配设备映射地址区间，把已认领设备的 BAR 映射到用户地址空间。 |
| `resource/irq.cc` | 配置 IRQ 路由，将中断转为端点通知，处理确认与解绑。 |
| `dmabuffer/dmabuffer.cc` | 分配连续物理块，建立 DMA 用户映射并挂入资源域。 |
| `storage/boot_image.cc` | 对名为 rootfs 的引导模块提供有界块读写。 |
| `time/time.cc` | 校准 TSC、读取 RTC，提供单调时间和墙钟时间。 |
| `ps2/ps2.cc` | 初始化 PS/2 控制器，读取键盘或鼠标的原始字节。 |
| `acpi/acpi.cc` | 校验 ACPI 表并按四字节签名查找。 |

用户态 device、pci、storage 服务分别负责设备协调、PCI 查询和存储驱动。内核不负责窗口、桌面、输入焦点等 GUI 策略。

## 引导显示信息和映射

`initDevices(bootInfo)` 从 BootInfo 复制帧缓冲信息；传入空指针时保存空信息。它只登记地址、尺寸、步长和像素布局，不进行图形绘制。

| 函数 | 作用与参数 |
| --- | --- |
| `describeDisplay(userAddress, info)` | 将显示参数写入 info。userAddress 是页对齐映射基址，返回地址还包含原始物理地址的页内偏移。无有效帧缓冲时返回 false。 |
| `mapFramebuffer(task, userAddress, info, generation)` | 为具有 Display 能力的 task 建立映射；地址为零时自动选择。info 接收显示参数，可选 generation 接收地址空间代际。失败返回 false。 |
| `reserveDisplayAddress(size)` | 原子分配设备映射使用的虚拟地址区间，按页取整并留一页间隔。越界返回零；它不创建 PTE，也不证明某个任务的该区间尚未使用。 |

映射先校验能力、页对齐、用户地址范围和帧缓冲尺寸，再在地址空间锁内逐页建立 PTE。可见数据为 pitch × height，最多 64 MiB；实际映射长度包含起始页内偏移并向上取整。映射页为用户可写、不可执行、禁用缓存的 external 页，不能按普通匿名页释放显存。中途失败时撤销本次已经建立的映射。

`describeDisplay` 只在可识别的 32 位通道布局下标记 Bgra 或 Rgba，其余布局标记 Unknown。调用者必须使用 pitch 和颜色位置信息，不能假定每行等于 width × 4。

## FrameMapping 的生命周期

FrameMapping 保存 owner_、address_、size_ 和 generation_。

| 成员函数 | 作用与参数 |
| --- | --- |
| `create(task, address, info)` | 分配包装对象并建立映射，成功后保留 task 引用；失败撤销已分配资源并返回空指针。 |
| `query(task, info)` | 要求调用者就是所有者，地址空间代际未变，且每页仍指向原来的帧缓冲物理页；通过后返回显示信息。 |
| `destroy()` | 只撤销仍属于该对象的 PTE，释放包装对象和所有者引用。 |

保存代际是为了区分同一 Task 在 exec 前后的地址空间；逐页核对物理地址则避免错误撤销已经被其他映射替换的页面。映射对象关闭不释放硬件帧缓冲本体。

## 串口与应急诊断

`initSerial()` 配置 COM1 的波特率、8 位字符、FIFO 和控制寄存器。`serialWrite(char)`、`serialWrite(const char*)` 在关闭本地中断并取得 outputLock 后输出，字符串换行补充回车。`serialWriteHex(value)` 输出带 0x 前缀的 16 位十六进制数，`serialWriteDecimal(value)` 输出十进制数。

底层 `writeCharacter(value)` 最多轮询发送状态 100000 次，然后写发送寄存器。因此它有等待上界，但不能保证异常硬件仍完整接收输出。

`emergencyWrite(text)` 和 `emergencyHex(value)` 绕过 outputLock。NMI 可能打断正在持锁的 CPU，应急诊断不能再次等待这把锁；代价是多个输出源的文本可能交错。

## 继续阅读

硬件访问授权见[设备机制服务](service.md)，设备停机与引用回收见[资源域](resource.md)，连续物理内存见[DMA](dmabuffer.md)，PCI 配置访问见[PCI](pci.md)。[存储](storage.md)、[输入](ps2.md)、[时间](time.md)和[ACPI](acpi.md)分别说明各自的执行过程与实现范围。
