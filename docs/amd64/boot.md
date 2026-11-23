# 内核启动与引导信息

入口实现为 `src/kernel/boot/head.S`、`boot/multiboot.cc` 和 `src/kernel/main.cc`。声明位于 `include/kernel/boot.h`，BootInfo 等类型及 parseMultibootInfo 保持在全局作用域，kernelEntry 使用 C 链接，kernelMain 位于 kernel。

## 文件与阶段

| 文件 | 内容 |
| --- | --- |
| `boot/head.S` | Multiboot2 头、32 位入口、能力检查、临时页表、长模式切换和引导栈。 |
| `boot/multiboot.cc` | 解析引导 tag，保存内存区、模块、帧缓冲、ACPI 及字符串引用。 |
| `main.cc` | 建立各内核模块的初始化依赖，创建初始执行环境并启动 init。 |

启动汇编使用独立临时 GDT，不能把其中选择子值直接当作运行时 CPU GDT 的选择子。CPU 初始化后会安装正式 GDT/TSS，见 [CPU](cpu/cpu.md)。

## 从 _start 到 kernelEntry

引导器按 Multiboot2 约定提供 EAX magic 和 EBX 信息地址。`_start` 关中断、保存信息地址、使用引导栈，并检查 CPUID、SSE/SSE2、长模式和 NX。无法满足要求时通过低层串口字符和停机路径结束，不依赖尚未建立的堆。

`setupPageTables` 建立一张 PML4、一张 PDPT 和 64 张页目录，用 2 MiB 页覆盖低端 64 GiB，同时提供低地址 identity 和高半区直接映射。identity 是过渡工具；完成高地址运行后由 initVir 删除低半区根项。

汇编设置 PAE、浮点控制和 EFER.LME/NXE，装入 CR3 后启用分页，通过远跳转进入 64 位代码。start64 清 DF、初始化浮点环境，把引导栈转换为直接映射地址，再调用 kernelEntry。传入参数依次是 Multiboot 地址、引导页表根、引导区域起点和内核加载终点。

先有临时页表和栈，才能执行依赖指针访问的 C++；先采用高半区，才能安全移除低 identity。这里不涉及用户 GUI、窗口或服务启动策略。

## 引导信息对象

| 类 | 保存的数据 |
| --- | --- |
| MemoryRegion | 物理 base、length 和固件 type。 |
| BootModule | 模块物理 start/end 和命令行引用。 |
| FramebufferInfo | 物理地址、pitch、宽高、bpp、类型及颜色位分布。 |
| BootInfo | 上述数组和数量、引导字符串、ACPI/ELF 引用、Multiboot 范围、页表根及引导保留范围。 |

getter/setter 分别读取和设置这些数据。数组记录由解析器的有界静态存储提供；命令行和 ACPI 等仍引用引导信息对应的存储，不是独立复制的任意长度对象。

`parseMultibootInfo(address, bootInfo)` 输入引导信息地址和结果对象，失败返回 false。它检查总长、tag 长度和边界，按 8 字节对齐推进；内存记录最多 128 项、模块最多 32 项，超过记录容量的条目不继续保存。成功还要求取得内存表。

解析涵盖命令行、引导器名称、模块、内存表、帧缓冲、ELF section 信息及新旧 ACPI 根。帧缓冲 tag 只有匹配类型和足够长度时读取颜色字段；ACPI 的签名和校验和由 Tables 使用时进一步验证。

## kernelMain 的依赖关系

内存阶段调用 initPhy、initVir、initHeap。物理页元数据不能从未初始化的堆申请；堆依赖物理分配和内核页表。

CPU 阶段建立 GDT/TSS、GS 和本地 APIC，随后启用依赖每 CPU 数据的 NUMA 选择和对象缓存，初始化时间、IDT、I/O APIC、帧缓冲信息和 BootImage。

并发阶段发现 SMP、登记引导 Thread、建立每 CPU 调度设施及 kernel Task，再设置系统调用入口和启动 AP。需要中断和调度的工作不能提前放在这些设施尚未就绪的位置。

机制阶段建立 Endpoint 和 CoreServices，准备隔离地址空间与基本执行路径，然后 startInitialProgram 加载 init。首个用户进程需要自己的页表、用户栈和初始内核服务句柄，不能直接跳到某个模块字节执行。

启动完成后引导线程通过 exitCurrentThread 退出；每 CPU 的 idle 线程负责空闲等待，避免一个无实际工作的引导线程持续占用调度份额。

## 初次可运行与清理

需要固定 CPU 的启动探针先以 makeRunnable=false 创建，再配置亲和性；迁移会将它交给目标运行队列。若先按默认掩码入队，线程可能已经运行甚至结束，后续设置无法再假定它是 Ready。探针在执行入口核对实际 CPU，而不只相信入队返回值。

新用户线程同样应在映像、栈、身份、初始句柄准备完成后才开放执行。加载失败走 Task 销毁和回收路径，不能只释放 Task 指针而留下地址空间或线程。

引导模块、Multiboot 信息和引导页表的物理范围由物理初始化保留。高半区地址别名本身不代表这些内存可以归还；必须先确认没有仍在使用的引导引用。程序映像装载细节见 [程序加载](user.md)，AP 启动见 [SMP](cpu/smp.md)。
