# x86-64 内核开发入口

内核实现位于 `src/kernel/`，声明位于 `include/kernel/`。本目录按实际执行过程说明：引导如何建立运行环境，内存与线程怎样互相依赖，用户请求怎样进入受控机制，以及资源何时才允许释放。

## 源文件和模块入口

下表目录均相对于 `src/kernel/`。各模块页面继续列出内部文件、类状态和函数。

| 文件或目录 | 负责的工作 | 文档 |
| --- | --- | --- |
| `main.cc` | 汇合初始化流程，建立机制对象并启动首个用户程序，最后退出引导线程。 | [启动](boot.md) |
| `boot/` | 从 Multiboot2 进入长模式，构造 BootInfo。 | [启动](boot.md) |
| `cpu/cpu.cc` | CPU 特性、GDT/TSS、当前 CPU 与本地 APIC。 | [CPU](cpu/cpu.md) |
| `cpu/interrupt/` | IDT、入口现场、异常分派与 I/O APIC 路由。 | [中断](cpu/interrupt.md) |
| `cpu/smp/` | 发现并启动 AP，协调跨 CPU TLB 失效与停止。 | [SMP](cpu/smp.md) |
| `memory/` | 物理页、对象分配、页表、共享对象和内存机制服务。 | [内存总览](memory/memory.md) |
| `task/` | Task/Thread、调度、同步、IPC、系统调用与任务机制服务。 | [任务总览](task/task.md) |
| `device/` | 帧缓冲、PCI、MMIO/DMA、IRQ、引导块设备、时钟、PS/2 和 ACPI 表。 | [设备总览](device/device.md) |
| `user/` | ELF 校验、装载、动态重定位和初始用户栈。 | [用户程序加载](user.md) |

头文件的命名空间以当前声明为准：CPU、中断、SMP 使用 kernel::cpu；mm.h、page.h 使用 kernel::mm；任务、IPC、同步和 syscall 使用 kernel::task；设备、PCI 使用 kernel::device；时间和 ACPI 分别使用 kernel::time、kernel::acpi；BootInfo 的声明在全局。

## 启动为何按这个顺序

`kernelEntry` 先初始化串口并解析 Multiboot2，记录启动页表和引导区范围，再调用 `kernelMain`。

kernelMain 先建立物理分配、页表管理和内核堆，因为 CPU、任务、端点等对象都需要内存；随后初始化 CPU、CPU 本地内存设施、时钟、IDT、中断控制器及引导设备信息。SMP 发现先于 AP 正式启动，线程、调度器、Task 和 syscall 环境准备好后，AP 才进入可调度状态。

端点和核心机制对象建立后，内核启用中断，通过 `startInitialProgram` 启动初始用户程序。地址空间、ELF、SMP 亲和性和 IPC 等启动检查也发生在这些阶段，具体位置见[启动流程](boot.md)。

最后由 `exitCurrentThread(0)` 结束引导线程。它不能作为无事可做但持续 Ready 的线程留在运行队列中，否则每次 I/O 唤醒都可能和空转引导线程争用时间片；真正无任务时由每 CPU 的 idle 线程执行休眠。

## 从用户请求到内核机制

用户通过 syscall 进入内核；IPC 系统调用操作端点，端点再调用机制对象或把请求送到用户态服务。内核中的 task.kernel、memory.kernel、device.kernel、pci.kernel 等是受控机制入口，不是与同名用户态服务混在一起的进程。

用户态 task 负责程序文件读取及启动协调，mm 负责 VMA 与缺页策略，device/storage 负责输入协调与存储驱动，FS 负责路径和文件系统。这些策略不因使用内核机制对象而转入内核；GUI 的窗口和桌面策略也不在内核内。

权限需要同时考虑调用者角色、能力、对象类型和句柄权限。消息里的 PID、地址和自报身份不能代替内核确定的 caller。普通程序加载不能通过参数自行取得 boot 服务身份。

阅读入口是[系统调用](task/syscall.md)、[IPC](task/ipc.md)、[核心服务入口](task/service.md)、[内存机制服务](memory/service.md)和[设备机制服务](device/service.md)。

## 地址空间与内存的关系

用户映射使用 4 KiB 页，地址下界为 0x10000，上界为 0x0000800000000000，不含上界。内核直接映射基址是 0xffff800000000000，NX 堆别名从 0xffff804000000000 开始。普通用户页保护遵守 W^X。

物理页由 Buddy 管理，小对象通过 Slab 和每 CPU 缓存分配；页对象、共享对象和页表各有不同引用。撤销 PTE 不总是释放物理页：共享页还可能有其他引用，external 页可能属于硬件或 DMA 资源域。

页表修改还需要本地及远端 TLB 失效。只有旧地址不再被 CPU 使用后，才能归还其页表和物理存储。设备 DMA 则有额外停机条件，不能用 CPU TLB 失效代替。

细节见[物理分配](memory/physical.md)、[内核对象](memory/object.md)、[虚拟映射](memory/virtual.md)、[共享对象](memory/shared.md)和[设备资源域](device/resource.md)。

## 线程、等待与资源退出

Task 保存地址空间、句柄、凭据、服务身份与资源统计；Thread 保存执行现场和调度状态。每 CPU 的 RunQueue 选择线程，红黑树维护就绪线程的虚拟运行时间顺序。运行中的线程不同时留在就绪树内。

等待路径要先在条件锁内登记等待，再释放锁并阻塞；唤醒可能先于真正阻塞，wakePending 用于衔接这个窗口。线程设为 Terminated 也不等于 CPU 已离开它的栈。

退出和 exec 要分别协调停止执行、退役栈、句柄引用、旧地址空间与设备停机。远端 CPU 停稳前不能释放旧线程栈和页表；设备复位失败时不能归还仍可能被 DMA 使用的内存。

细节见[调度](task/sched.md)、[同步](task/sync.md)、[资源管理](task/resource.md)、[凭据](task/credentials.md)和[核心转储](task/core.md)。

## 硬件入口的实现范围

| 功能 | 当前范围 |
| --- | --- |
| PCI | segment 0、传统 256 字节配置空间，内存 BAR 探测和受限 command 写入。 |
| DMA 域 | 当前可信复位流程针对现代 VirtIO block，失败保留页面并隔离。 |
| IRQ 绑定 | 传统 IRQ 0～15，端点通知及电平触发确认。 |
| PS/2 | 初始化控制器并读取原始字节，由用户态组包和解析。 |
| 引导存储 | rootfs 模块的内存块读写，不向原始 ISO 持久化。 |
| 时间 | TSC 单调时间与启动 RTC 墙钟基准。 |
| ACPI | 表校验与查找，没有 AML、电源状态或休眠实现。 |

对应文档为[PCI](device/pci.md)、[DMA](device/dmabuffer.md)、[输入](device/ps2.md)、[存储](device/storage.md)、[时间](device/time.md)和[ACPI](device/acpi.md)。文件职责、调用顺序和当前限制应结合阅读，避免把声明存在误认为完整功能已经实现。
