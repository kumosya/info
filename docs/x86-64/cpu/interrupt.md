# 中断、异常与返回路径

实现位于 `src/kernel/cpu/interrupt/entry.S`、`interrupt.cc`、`exceptions.cc`、`ioapic.cc`，声明在 `include/kernel/interrupt.h`，C++ 命名空间为 `kernel::cpu`。

## 文件分工与现场

entry.S 为全部 256 个向量生成入口，并统一压入 vector 和 errorCode。硬件不提供错误码的入口补零，提供错误码的向量不重复压入。随后保存 15 个通用寄存器，形成 Context 的固定顺序。

Context 还包含 RIP、CS、RFLAGS、RSP、SS。处理器和汇编共同形成该帧，修改字段顺序会破坏恢复；不能将普通 C++ 私有状态对象的布局习惯直接套到硬件帧。

公共入口清 DF、对齐 C++ 调用栈并保存恢复时的原帧指针。NMI 可在内核 swapgs 窗口进入，所以入口检查实际 GS base，必要时交换并记录是否交换；返回只撤销自己做过的交换，然后恢复寄存器、去掉 vector/error 并 IRET。

## IDT 和处理函数

| 函数 | 功能与参数 |
| --- | --- |
| `initIdt()` | 屏蔽旧 PIC，建立 256 项入口并加载 BSP IDT；只有断点向量开放用户 DPL3。 |
| `loadIdt()` | 当前 CPU 没有 IDT 时复制引导表，再执行 lidt；AP 使用自己的表。 |
| `registerHandler(vector, handler)` | 锁内登记非空且尚未占用的函数，返回是否成功。 |
| `unregisterHandler(vector)` | 锁内清除登记，读取方按原子发布规则观察。 |
| `interruptDispatch(context)` | 区分异常与 IRQ/IPI，调用处理器、EOI、必要的调度和用户信号返回。 |

IST1 给双重故障，IST2 给 NMI，IST3 给机器检查，避免普通线程栈损坏时连诊断入口都无法建立。登记处理函数不等于硬件源已经解除屏蔽；两者由设备绑定过程协调。

## 异常分类

`handleException(context)` 对 NMI、双重故障和机器检查直接 panic；#PF 转入 handlePageFault；其余由 dispatchUserException 判断能否归责用户执行，否则 panic。

exceptions.cc 将除零/整数溢出、非法指令、调试/断点、可归责保护异常、对齐和实际未屏蔽浮点异常转为相应信号。它保留硬件 RIP，不统一调整断点或单步返回位置。DR6、x87 状态和 MXCSR 提供具体原因。

用户 CS 不是所有异常都可恢复的充分条件。保留位、描述符机制错误及未启用的平台特性按当前策略诊断，不伪造普通用户错误。同步信号现场和恢复见 [信号与 core](../task/core.md)，缺页与 Pager 见 [内存服务](../memory/service.md)。

## 外部路由

ioapic.cc 从 MADT 读取 I/O APIC 及中断覆盖，先将所有已知引脚屏蔽，后由具体请求设置路由。当前保存最多 8 个 I/O APIC 和 16 条 legacy IRQ 对应关系；没有有效 MADT/I/O APIC 时保持外部路由禁用。

`initControllers(boot)` 建立这些表；`setIrq(irq, vector, apicId, levelTriggered)` 把 IRQ 经覆盖转换为 GSI，再定位 I/O APIC 引脚，设置目标 APIC、向量、极性和触发方式；`maskIrq(irq)` 屏蔽对应引脚。MMIO 的选择寄存器和数据寄存器必须在 ioApicLock 下配对访问。

本地 APIC 定时器与 IPI 不通过这张 legacy IRQ 路由表。不要把 IRQ 编号、IDT vector、GSI 和 APIC ID 当成同一个值。

## EOI、抢占和用户返回

向量 32 以上先保存用户现场，再调用已登记函数；未登记的非 spurious 向量首次输出诊断并 EOI，0xff 不写 EOI。设备中断源确认仍由驱动负责，CPU 的 EOI 不代表可以立即安全重开一个电平源。

在调度器已初始化、当前允许抢占且有重调度请求时，interruptDispatch 才调用 schedule。返回用户前检查 pending 信号，因此计算循环无需主动 syscall 也能接收信号。

`panic(context)` 关中断，用一次性标志限制重复诊断，请求其他 CPU 停止，使用无锁应急串口输出后 halt。此路径不等待 FS、不分配普通业务对象，也不执行常规 Task 销毁。
