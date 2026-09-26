# 中断子系统（src/kernel/interrupt/）

本文档描述 `src/kernel/interrupt/` 的实现：IDT 管理（`interrupt.cc`）、中断/异常分发（`entry.S`）与 I/O APIC 路由（`ioapic.cc`）。对应头文件 `include/kernel/interrupt.h`。

## 1. 中断上下文（InterruptContext）

`entry.S` 压栈顺序与结构体字段一一对应（22 个 `uint64_t`）：

```cpp
struct InterruptContext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t errorCode;
    uint64_t rip, cs, rflags, rsp, ss;   // CPU 自动压栈部分
};
```

## 2. ISR 桩（entry.S）

- `isrNoError vector`：`pushq $0`（补假错误码）+ `pushq $vector` → `isrCommon`。
- `isrError vector`：CPU 已压错误码，仅 `pushq $vector`。
- 带真实错误码的异常向量：**8, 10, 11, 12, 13, 14, 17, 21, 29, 30**；其余 0-31 用 `isrNoError`。
- 向量 32-255 全部以 `isrNoError` 生成。
- `isrStubTable`：256 个桩地址数组，供 `initIdt` 建表。

### `isrCommon` 公共入口

1. `cld`；依次压栈 15 个通用寄存器（rax…r15，与 `InterruptContext` 顺序一致）。
2. 检查被中断上下文 CS 的 RPL（偏移 144）；用户态则 `swapgs`（切换到内核 per-CPU 数据）。
3. `rdi = rsp`（传 `InterruptContext*`），栈按 16 字节对齐，`call interruptDispatch`。
4. 用户态则再次 `swapgs` 换回；逆序弹出寄存器，跳过 vector 与错误码，`iretq`。

## 3. IDT 管理（interrupt.cc）

```cpp
void initIdt();
void loadIdt();
bool registerInterruptHandler(uint8_t vector, InterruptHandler handler);
void unregisterInterruptHandler(uint8_t vector);
```

`initIdt`：

1. 全屏蔽 8259A 传统 PIC（向 0x21/0xa1 写 0xff）。
2. 填充 256 个门：段选择子固定 `0x08`，属性 `0x8e | (privilege << 5)`（Present + 64 位中断门）。
3. **IST 分配**：向量 8（#DF）→ IST 1；向量 2（NMI）→ IST 2；向量 18（#MC）→ IST 3；其余 IST 0。
4. **DPL=3 的门**：向量 3（#BP）与向量 0x80；其余 DPL 0。
5. `handlers` 全部置空；`loadIdt` 加载。

`loadIdt`：若当前 CPU 尚无 IDT，分配并从 `bootstrapIdt` 复制 256 项（**每 CPU 独立 IDT 副本**）；`lidt` 加载。BSP 初始直接使用静态 `bootstrapIdt`，AP 通过 `loadIdt` 分配副本。

`registerInterruptHandler`：加锁；handler 为空或向量已被占用返回 false；以 RELEASE 原子写。

## 4. 中断分发

```cpp
extern "C" void interruptDispatch(InterruptContext* context);
```

1. `vector < 32` → `handleException`。
2. 否则原子（ACQUIRE）读取处理函数并调用。
3. **向量 `0xff`（虚假中断）不发 EOI**，其余 `sendEndOfInterrupt()`。
4. 若调度器已初始化、抢占使能、且当前 CPU `rescheduleRequested` → 调用 `schedule()`（中断返回前完成调度切换）。

### 异常处理 `handleException`

- 向量 14 → 转 `handlePageFault`（见 [memory.md](memory.md)）。
- 其余先打印 vector/error/rip。
- **向量 3 且来自内核态**：直接返回（内核断点可恢复）。
- 用户态异常 → `terminateCurrentThreadFromException(-vector)`。
- 内核态不可恢复异常 → 打印后 `haltCpu()`。

## 5. I/O APIC（ioapic.cc）

```cpp
void initInterruptControllers(const BootInfo* bootInfo);
bool configureIrq(uint8_t irq, uint8_t vector, uint32_t apicId);
void maskIrq(uint8_t irq);
```

### 初始化

1. IRQ 0-15 默认恒等路由（`irqRoutes[irq].gsi = irq, flags = 0`）。
2. 解析 MADT：
   - **type 1（I/O APIC）**：注册 MMIO 基址与 GSI 基址；重定向数量 = 寄存器 1 的 bits 16-23 + 1；最多 8 个。
   - **type 2（中断源覆盖）**：`bus == 0 且 source < 16` 时覆盖 IRQ→GSI 路由与极性/触发标志。
3. 屏蔽所有 I/O APIC 的全部引脚。
4. MADT 不可用或无 I/O APIC 时打印警告并返回（禁用外部 IRQ 路由）。

I/O APIC 寄存器经间接寻址：索引寄存器 = MMIO 偏移 0x00，数据寄存器 = 偏移 0x10。

### IRQ 配置 `configureIrq(irq, vector, apicId)`

- 要求 `irq < 16` 且 `vector >= 32`。
- 按路由表找到 GSI 所属 I/O APIC 与引脚。
- 极性：`flags & 0x3 == 3`（低电平有效）→ 重定向低字置 bit 13。
- 触发：`(flags >> 2) & 0x3 == 3`（电平触发）→ 置 bit 15。
- 高字写目标 `apicId << 24`，低字写 `vector | 极性/触发位`（固定投递模式，未屏蔽）。

`maskIrq(irq)`：屏蔽其路由到的 GSI 引脚（置重定向低字 bit 16）。

## 6. 向量分配汇总

| 向量 | 用途 |
|---|---|
| 0-31 | CPU 异常 |
| 32 | Local APIC Timer（调度器节拍，1 ms） |
| 64-79 | IRQ 0-15（设备中断绑定） |
| 0x80 | 保留（DPL 3，未使用） |
| 0xf0 | SMP 重调度 IPI |
| 0xf1 | SMP TLB shootdown IPI |
| 0xff | Local APIC 虚假中断 |
