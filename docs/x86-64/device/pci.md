# PCI 配置访问

声明在 `include/kernel/pci.h`，实现分为 `src/kernel/device/pci/access.cc` 和 `service.cc`。PciBus 与 Pci 位于 `kernel::device`；机制服务入口 `pciKernelService()` 位于 `kernel::task`。

## 总线对象与单个功能

PciBus 保存配置访问锁 lock_。`pciBus()` 返回内核共用的总线对象。Pci 保存总线引用 bus_ 和一份功能身份 function_，使后续读写不必重复传入总线、设备和功能编号。

| 函数 | 作用与参数 |
| --- | --- |
| `PciBus::enumerate(functions, capacity)` | functions 为输出数组，capacity 为容量；扫描并返回实际写入的功能数，达到容量即停止。 |
| `PciBus::read(function, offset, value)` | function 确定目标，offset 为配置空间字节偏移，value 接收 32 位值；非法地址返回 false。 |
| `PciBus::write(function, offset, value)` | 向该功能的 offset 写入 32 位 value；校验失败返回 false。 |
| `PciBus::getMemoryBar(function, barIndex, base, size)` | 探测内存 BAR，base、size 接收物理基址和长度；不支持或无效时返回 false。 |
| `Pci(bus, function)` | 绑定总线与目标功能，不进行设备认领，也不取得独占权。 |
| `Pci::read(offset, value)`、`write(offset, value)`、`getMemoryBar(index, base, size)` | 使用对象保存的目标身份执行对应总线操作。 |

配置访问使用 mechanism 1：先写 0xcf8 的地址，再通过 0xcfc 读写数据。地址与数据操作必须处于同一临界区，否则另一 CPU 改写地址寄存器后会导致访问错误设备。

当前仅接受 segment 0，device 小于 32，function 小于 8；偏移须小于 256 且按四字节对齐。没有 ECAM 扩展配置空间访问。

## 枚举和 BAR 探测过程

枚举扫描 256 个 bus，每个 bus 扫描 32 个 device。先读 function 0，vendor ID 表示不存在时跳过；只有 headerType 的多功能标志置位时才继续扫描该设备的其他七个功能。输出包括位置、vendor/device ID、类别、子类、编程接口和头部类型。枚举结果只是存在性信息，不代表调用者已经获得设备控制权。

BAR 大小不能从原值直接得到。`getMemoryBar` 在配置锁内保存 command 和 BAR，暂时关闭 I/O 与内存解码，向 BAR 写全 1 后读出掩码，再恢复原值。由掩码求出大小，避免探测值长期留在设备中。

实现支持 32 位和 64 位内存 BAR，拒绝 I/O BAR；64 位 BAR 需要连续两个槽，不能从槽 5 开始。探测会暂时改变设备配置，应在受控认领过程中完成，不能由正在驱动设备的多个调用者任意并行执行。

## pci.kernel 请求

内部 `PciService::call(caller, request, reply)` 要求 Pci 角色、Pci 能力及至少四个数据字，之后按操作分派。

| 请求 | 数据和执行方式 |
| --- | --- |
| `Enumerate` | data[0] 为容量，范围 1～64；data[0] 返回数量，data[1] 起为功能数组。 |
| `ReadConfig` | data[0] 为偏移，data[1] 起为功能身份；通过 Pci 读取，reply.data[1] 返回值。 |
| `WriteConfig` | 至少五个字；data[0] 为偏移，data[1] 起为功能身份，data[3] 为值，data[4] 为设备所有者标识；通过 ResourceDomains 过滤写入。 |

权限不足或消息过短返回 -3；不支持的操作、非法参数或配置写入被拒绝返回 -1。成功分支返回完整回复数据区。

`ResourceDomains::writeConfiguration` 限制已认领设备的配置写入，尤其防止在没有资源域控制时开启 bus mastering。这里的 owner 标识不会让请求者任意冒充 PCI 服务：外层固定角色检查仍然必须通过。完整规则见[资源域](resource.md)。
