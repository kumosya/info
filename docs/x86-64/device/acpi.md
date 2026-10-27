# ACPI 表查找

实现为 `src/kernel/device/acpi/acpi.cc`，声明为 `include/kernel/acpi.h`，命名空间是 `kernel::acpi`。当前功能是验证并查找固件描述表，不包含 AML 解释器、ACPI 关机、休眠或设备电源管理。

## Tables 与固件布局

`Tables(boot)` 保存 BootInfo 指针 boot_，不复制固件表，也不拥有表内存。`find(signature)` 接受恰好四字节的表签名，成功返回直接映射中的只读 AcpiSdtHeader 指针，失败返回空指针。signature 不是任意长度的路径或字符串。

AcpiSdtHeader 是按 ACPI 规定布局打包的结构，记录签名、长度、版本、校验和、OEM 与创建者信息。它直接对应固件字节格式，因此必须保留精确布局；Tables 才是承载查找行为与状态的对象。

## 查找过程与校验顺序

查找先确认 BootInfo 与 ACPI 根存在，验证 RSDP 的 RSD PTR 签名和最初 20 字节校验和。ACPI 2.0 及以后版本若扩展长度和校验和有效，优先使用 XSDT；未找到所需表时回退 RSDT。

XSDT 使用八字节物理地址条目，RSDT 使用四字节条目。读取条目时逐字节按小端拼接，避免把固件中未对齐的地址数组直接作为本机指针数组。每个候选 SDT 都要通过长度与校验和检查，再比较签名；长度必须至少能容纳公共头，且不超过 1 MiB。

| 内部函数 | 功能与参数 |
| --- | --- |
| `bytesEqual(left, right, length)` | 对指定长度的字节逐个比较，用于签名判断。 |
| `checksumValid(address, length)` | 累加指定区域的字节，低八位为零才接受。 |
| `readLittleEndian(address, size)` | 将 size 个字节解释为小端整数，用于根表地址条目。 |
| `mapPhysical<Type>(physicalAddress)` | 将位于当前 64 GiB 直接映射范围内的物理地址转换为对应类型指针；不分配新映射。 |
| `tableValid(table)` | 检查公共头长度范围和整表校验和。 |
| `findInRoot(rootPhysicalAddress, extended, signature)` | 校验根表后遍历条目；extended 决定条目宽度，signature 决定目标。 |

这些检查用于拒绝明显不合法的表。直接映射地址范围检查本身不证明固件所报的整个区域都属于可安全读取的 RAM，不能将当前实现描述成可以处理任意恶意固件地址的完整防护。

## 使用者和边界

CPU 与中断模块使用 MADT 取得处理器、I/O APIC 和中断覆盖信息；内存拓扑模块使用 SRAT 建立内存节点关系。Tables 不解释这些表的全部条目，也不负责修改 CPU 拓扑。

boot_ 只在查找期间需要有效；find 返回的指针依赖底层固件表内存继续保留。调用者使用具体表内容前，还要检查相应条目长度和边界。初始化关系见[CPU](../cpu/cpu.md)、[SMP](../cpu/smp.md)和[物理内存](../memory/physical.md)。
