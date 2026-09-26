# osinfo 内核总览（x86-64）

本文档描述 `src/kernel/` 下 x86-64 内核的总体结构。内核文档按源码目录组织，每个目录对应一份文档。

## 1. 架构定位

osinfo 运行在 x86-64 架构，采用微内核风格：

- 内核直接提供：内存管理、线程/进程管理、调度、中断、系统调用、IPC 原语（端点/管道/伪终端/消息队列/共享内存）、信号、时间与设备访问控制。
- 文件系统、终端、窗口、输入、存储等高层服务全部运行在用户态，通过端点 IPC 访问。
- 用户态程序通过 73 个系统调用（`SYSCALL`/`SYSRET`）进入内核。

## 2. 源码目录与文档对应

| 源码目录                       | 内容                              | 文档                           |
| -------------------------- | ------------------------------- | ---------------------------- |
| `src/kernel/boot/`         | Multiboot2 引导、长模式进入、串口          | [boot.md](boot.md)           |
| `src/kernel/cpu/`          | CPU 初始化、GDT/TSS、Local APIC、ACPI | [cpu.md](cpu.md)             |
| `src/kernel/cpu/smp/`      | 多核发现与 AP 启动                     | [smp.md](smp.md)             |
| `src/kernel/memory/`       | 物理页、页表、内核堆、缺页、共享内存              | [memory.md](memory.md)       |
| `src/kernel/interrupt/`    | IDT、异常、I/O APIC                 | [interrupt.md](interrupt.md) |
| `src/kernel/task/`         | 线程、进程、调度器、信号                    | [task.md](task.md)           |
| `src/kernel/task/ipc/`     | 端点、管道、伪终端、消息队列                  | [ipc.md](ipc.md)             |
| `src/kernel/task/sync/`    | 同步原语、用户态等待（futex）               | [sync.md](sync.md)           |
| `src/kernel/task/syscall/` | 系统调用入口与全部 73 个调用                | [syscall.md](syscall.md)     |
| `src/kernel/time/`         | TSC/RTC 时钟                      | [time.md](time.md)           |
| `src/kernel/device/`       | PCI、端口、显存、中断绑定                  | [device.md](device.md)       |
| `src/kernel/user/`         | ELF 加载、动态链接、用户态启动、POSIX 桩       | [user.md](user.md)           |

## 3. 关键全局约定

以下常量贯穿整个内核，各子系统共同遵守：

| 约定                   | 值                    | 说明                              |
| -------------------- | -------------------- | ------------------------------- |
| 直接映射基址               | `0xffff800000000000` | 前 64 GB 物理内存恒等映射到高半区            |
| 内核堆基址                | `0xffff804000000000` | 直接映射基址 + 1 GB，大小 16 MB          |
| 用户地址下限               | `0x10000`            | 低于此地址的映射一律拒绝                    |
| 用户地址上限               | `0x0000800000000000` | 47 位规范地址分界（128 TiB）             |
| 页大小                  | 4096                 | 用户映射仅支持 4 KB 页                  |
| W^X 策略               | —                    | 可写页必须同时带 `pageNoExecute`，禁止写+执行 |
| 内核代码段选择子             | `0x08`               | GDT 第 1 项                       |
| 内核数据段选择子             | `0x10`               | GDT 第 2 项                       |
| 用户数据段选择子             | `0x1b`               | GDT 第 3 项（DPL 3）                |
| 用户代码段选择子             | `0x23`               | GDT 第 4 项（DPL 3）                |
| TSS 选择子              | `0x28`               | GDT 第 5-6 项（64 位 TSS 占两槽）       |
| 重调度 IPI 向量           | `0xf0`               | SMP 核间重调度请求                     |
| TLB shootdown IPI 向量 | `0xf1`               | 跨核页表项失效                         |
| 虚假中断向量               | `0xff`               | Local APIC spurious，不发 EOI      |
| 定时器中断向量              | `32`                 | Local APIC Timer，1 ms 周期        |
| IRQ 中断向量基址           | `64`                 | IRQ 0-15 → 向量 64-79             |

## 4. 启动初始化序列

`kernelEntry`（head.S 的 64 位入口）→ `kernelMain`，初始化顺序严格如下：

```text
 1. initPhysicalMemory      物理页位图
 2. initVirtualMemory       移除低端恒等映射，保留高半直接映射
 3. initKernelHeap          16 MB bump 堆
 4. initCpu                 CPUID/GDT/TSS/Local APIC
 5. initTime                TSC 校准 + RTC 墙钟
 6. initIdt                 256 项 IDT
 7. initInterruptControllers  I/O APIC 发现与屏蔽
 8. initDeviceSubsystem     帧缓冲信息记录
 9. initSmp                 MADT 解析、CPU 描述符
10. initThreadSubsystem     引导线程
11. initScheduler           每 CPU 红黑树运行队列 + 1 ms 定时器
12. initProcessSubsystem    内核进程
13. runSynchronizationSelfTest  同步原语自检
14. initSyscall             SYSCALL/SYSRET MSR
15. startApplicationProcessors  INIT/SIPI 启动 AP
16. SMP 调度探测（多核时）
17. 地址空间探测 + ELF 校验自检
18. Ring 3 系统调用探测进程
19. initIpc + IPC 收发探测线程
20. 调度器探测线程 ×2
21. enableInterrupts
22. startInitialUserland    加载 init 进程
23. hlt 空闲循环
```

任何一步失败都会打印串口诊断并停机（`cli; hlt`）。

## 5. 能力模型

进程通过能力位（`Process::capabilities_`）获得特权操作资格：

| 能力                         | 值        | 用途                                  |
| -------------------------- | -------- | ----------------------------------- |
| `processCapabilityDevice`  | `1 << 0` | PCI 枚举/配置、MMIO 映射、DMA、IRQ 绑定、I/O 端口 |
| `processCapabilitySpawn`   | `1 << 1` | SpawnProgram/SpawnImage 创建子进程       |
| `processCapabilityDisplay` | `1 << 2` | 帧缓冲映射                               |

能力由内核在加载内置服务时授予（见 [user.md](user.md)），或由 `SpawnImage` 的父进程传递（仅允许 Display 位）。

## 6. 句柄模型

内核对象（端点、进程、管道、伪终端、消息队列、共享内存、MMIO 区域、DMA 缓冲、中断绑定）通过每进程句柄表访问：

- 句柄是 64 位值：高 16 位 generation（防悬空），低 32 位为槽位号（从 1 起）。
- 权限位：`handleRightRead = 1<<0`、`handleRightWrite = 1<<1`、`handleRightManage = 1<<2`。
- 关闭句柄时，Pipe/Pty/MessageQueue/SharedMemory 四类会释放底层对象引用；其余类型仅移除表项。
- 每进程初始 64 个句柄槽。

详见 [task.md](task.md) 的句柄表章节。

## 7. 锁与并发约定

- `SpinLock`：test-and-set + `pause` 自旋，单字节。
- `InterruptSpinLockGuard`：关中断 + 加锁的 RAII 守卫。
- `PreemptionSpinLockGuard`：关抢占（`preemptionCount` 计数）+ 加锁的 RAII 守卫。
- `Atomic<T>`：基于 GCC `__atomic_*` 内建，支持五种内存序。
- 中断处理函数尾部检查 `rescheduleRequested` 并调用 `schedule()`，实现中断上下文延迟调度。
- 内核堆与物理页分配器均为全局单锁。

## 8. 诊断

所有早期与内核诊断输出走 COM1 串口（115200 8N1），前缀约定：

```text
[BOOT] [CPU] [MEMORY] [SCHED] [SMP] [INTERRUPT] [IPC]
[SYSCALL] [ELF] [USERLAND] [TIME] [DISPLAY] [SYNC]
```

