# 多处理器启动与协作

实现位于 `src/kernel/cpu/smp/smp.cc` 和 `trampoline.S`，声明在 `include/kernel/smp.h`，命名空间为 `kernel::cpu`。此模块负责 CPU 发现、AP 启动和 IPI，不替代 task 的调度策略。

## CPU 描述与发现

CpuDescriptor 私有保存逻辑 cpuId、固件处理器标识、硬件 apicId、启用状态、原子 CpuState、startupStage 和 CpuLocalData 指针。getter/setter 涉及这些数据。CpuState 包含 Offline、Starting、Online、Stopping、Failed；有枚举值不代表所有热插拔状态转换都已实现。

`initSmp(boot)` 从 ACPI MADT 读取已启用的 Local APIC/x2APIC 条目，为 BSP 建立编号 0，并在上限 64 个 CPU 内记录 AP。无可用表时保留 BSP 单核环境。逻辑编号用于掩码和数组，APIC ID 用于实际 IPI 目的地，不能相互代换。

`getDiscoveredCpuCount()` 与 `getOnlineCpuCount()` 分别表示记录的处理器数和已经完成启动的数量；只有后者及每个描述符状态能证明 CPU 可以接收工作。

## AP trampoline

trampolineAddress 为物理 0x8000，启动向量由其页号得到。startApplicationTaskors 在内核根中临时建立所需低地址映射，把不超过一页的 trampoline 复制过去，然后逐个启动 AP。

共享启动槽保存目标 CR3、栈、CpuLocalData 和 C++ 入口。AP 从 16 位实模式进入保护模式，再开启 PAE、长模式/NX 和分页；进入 64 位后装载专用栈及 GS，调用 applicationTaskorEntry。

必须在发布这些参数后再发送 INIT/SIPI；当前串行启动并等待每个 AP 的结果，避免第二个 AP 覆盖第一个尚未读取的共享槽。BSP 使用有界忙等待和 startupStage 报告失败，不把未完成启动的 AP 算作 Online。结束后解除临时低地址映射。

## C++ 启动握手

`applicationTaskorEntry(cpu)` 依次建立该 CPU 的 GDT/TSS、本地 APIC、IDT、syscall MSR、引导 Thread 和 RunQueue。直到这些可供调度设施存在，才以 Release 发布 Online 并增加在线计数。

读取方以 Acquire 观察状态，再使用已发布 localData。提前把 CPU 标为 Online 会让远端把线程入队到尚不存在的队列。AP 引导线程随后退出，空闲由该 CPU 的 idle 线程承担。

源码保留 `startApplicationTaskors`、`initApplicationTaskorCpu` 等现有拼写，文档按实际符号查找，不另行创造 API 名。

## 远端调度与亲和性

| 函数 | 功能与参数 |
| --- | --- |
| `getCpuDescriptor(cpuId)`、`getCpuLocalData(cpuId)` | 按逻辑编号取得描述或本地数据，越界返回空。 |
| `requestRemoteReschedule(cpuId)` | 仅向非本 CPU 且 Online 的目标发送向量 0xf0。 |
| rescheduleIpiHandler | 设置目标 CPU 的重调度请求，由允许抢占的返回路径完成切换。 |

亲和掩码的位表示逻辑 CPU。Thread::migrate 检查目标位并在重新入队前保存目标编号；不能检查刚被新掩码排除的旧 CPU 位。

启动亲和性探针先创建 detached Ready 线程，再 setAffinity 到 CPU 1，迁移完成其首次入队；入口再核对实际 CPU。否则默认掩码下提前入队的探针可能在配置前已经执行或结束。调度细节见 [调度器](../task/sched.md)。

## TLB shootdown

`requestTlbShootdown(virtualAddress)` 在多核环境下串行发布一个失效地址、清确认计数，向其他 Online CPU 发送向量 0xf1，并等待全部确认。接收处理器 invlpg 后增加确认计数；向量与计数不是用户可修改的消息状态。

申请全局 shootdown 锁前禁止本线程抢占，但保持中断可达；若先关中断再争锁，两个 CPU 可能各自等待对方处理 IPI。取得锁后才保存并关闭本地中断，完成发布、发送及确认，再解锁并恢复状态。

当前实现等待全部目标确认，没有把超时当作“远端翻译已消失”的回退。调用者必须避免持远端完成路径必需的锁，并保持 CPU 在线协议前提。页表锁应在等待前释放，物理页面不能在失效完成前复用。

TLB 确认只说明该地址翻译失效，不代表目标 Task 已完全离开 CPU。旧线程栈和整个根释放仍需要 task::quiesceTask，也不能由此证明设备 DMA 已停止。
