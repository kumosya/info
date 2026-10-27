# CPU 状态与执行环境

主体实现位于 `src/kernel/cpu/cpu.cc`，声明在 `include/kernel/cpu.h`，命名空间为 `kernel::cpu`。本页负责处理器自身状态；中断路由见 [中断](interrupt.md)，处理器启动协作见 [SMP](smp.md)。

## 类与保存状态

| 类或固定布局 | 内容 |
| --- | --- |
| CpuFeatures | 长模式、NX、SSE/SSE2、APIC/x2APIC、TSC、invariant TSC 的探测结果。 |
| CpuLocalData | 自指针、逻辑 CPU/APIC 编号、当前线程/Task、运行队列、内核栈、用户 syscall 栈暂存、TSS、IDT、抢占计数和重调度状态。 |
| CpuArchData | 本 CPU 的 GDT 和 TSS。 |
| PreemptionGuard | 持有所属 CpuLocalData 引用，在作用域内增加抢占禁止计数。 |
| TssTable / DescriptorTablePointer | CPU 指令直接读取的固定字段和 packed 布局。 |

读取接口涉及上述能力和 CPU 本地数据；设置接口涉及编号、当前执行对象、栈、IDT 及调度标志。固定布局不能因对象整理改变硬件偏移。

## 初始化与特权级栈

`CpuFeatures::detect()` 使用 CPUID，不把“支持 x2APIC”当作“已使用 x2APIC”。`init()` 检查必需能力，建立 BSP 栈、正式 GDT/TSS、GS 与本地 APIC。

正式 GDT 中内核代码选择子为 0x08，内核数据为 0x10，用户数据/栈带 RPL3 为 0x1b，用户代码带 RPL3 为 0x23。TSS 提供 rsp0 和三条独立 IST 栈，分别给双重故障、NMI、机器检查。正常应急栈分配大小为 16 KiB，AP 准备栈为 32 KiB；线程内核栈由 task 模块另行管理。

| 成员函数 | 功能与参数 |
| --- | --- |
| `CpuLocalData::prepare(cpuId, apicId)` | 为待启动 CPU 准备本地对象、架构数据和栈，失败返回空。 |
| `initDescriptorTables(arch)` | 填写本 CPU GDT/TSS，分配 IST 并加载描述符表。 |
| `initApplicationTaskorCpu()` | AP 安装自己的 GS、GDT/TSS 和本地 APIC。 |
| `setKernelStack(stackTop)` | 同时更新每 CPU syscall 栈指针与 TSS.rsp0，供下一次用户进入。 |

切换线程前必须更新目标栈；只修改 TSS 不够，因为 SYSCALL 不自动按 TSS 切栈。分配 CPU 数据成功也不等于 AP 已 Online，发布时机由 SMP 协议决定。

## GS 与汇编约定

内核态 GS 指向当前 CpuLocalData。`getCurrent()` 从 gs:0 读取自指针，`getCurrentId()` 读取逻辑编号；这些接口在 GS 尚未建立时不可用。

KERNEL_CPU_STACK_OFFSET 为 48，KERNEL_CPU_USER_STACK_OFFSET 为 88。entry.S 使用这两个共享定义，CpuLocalData 的布局断言约束它们。用户进入或返回通过 swapgs 交接 GS，NMI 可能落在交接窗口，因此中断入口不能仅根据保存的 CS 推断当前 GS 属于谁。

## 浮点与特权寄存器

BSP/AP 设置 CR0、CR4 的浮点和 SSE 位，采用非惰性 FXSAVE64 策略；每线程保存 512 字节，由 Thread::switchTo 和信号返回恢复。当前未建立 XSAVE/AVX 扩展上下文管理。

`readMsr(msr)`、`writeMsr(msr, value)` 封装特权寄存器访问，只用于已知机制；它们不是对用户程序开放的任意 MSR 服务。

## 本地 APIC 与中断控制

| 函数 | 功能与参数 |
| --- | --- |
| `initLocalApic()` | 启用并采用 MMIO 本地 APIC，设置 spurious 向量 0xff。 |
| `initTimer(vector, initialCount)` | 设置分频、周期模式和初始计数；计数不是固定毫秒承诺。 |
| `sendEndOfInterrupt()` | 写 EOI，表示 CPU 侧服务结束，不等于设备已清除中断源。 |
| `sendInterTaskorInterrupt(apicId, vector)` | 向指定硬件 APIC 发送普通 IPI。 |
| `sendInitInterTaskorInterrupt(apicId)`、`sendStartupInterTaskorInterrupt(apicId, vector)` | AP 启动信号。 |
| `stopOtherCpus()` | 应急路径向其他 CPU 请求 NMI 停止，不执行普通服务资源回收。 |
| `halt()` | 关中断并永久停机；与允许中断的调度 idle 不同。 |

## 抢占、关中断与原子操作

`disableInterrupts()` 保存标志并关本 CPU 中断，`restoreInterrupts(flags)` 恢复，`enableInterrupts()` 显式开中断。它们不阻止其他 CPU 访问共享数据。

`disablePreemption()`、`enablePreemption()` 管理本 CPU 嵌套计数；`isPreemptionEnabled()` 在计数为零时允许中断返回调度。PreemptionGuard 仅限制线程切换，仍允许中断/IPI，因此适合必须让远端失效继续完成的提交段。

`include/kernel/atomic.h` 的 Atomic 和 MemoryOrder 位于全局作用域。Atomic 私有保存一个对齐值，提供 load、store、exchange、compareExchange、fetchAdd、fetchSub。compareExchange 成功写 desired，失败更新 expected；fetch 操作返回修改前值。

Relaxed 只保证该值原子操作，Release/Acquire 用于发布与观察初始化完成状态，AcquireRelease 用于读改写，Sequential 为默认顺序。调用者要按操作合法性选择内存序；原子成员不自动保护旁边的普通字段，也不代替跨多个字段的锁。
