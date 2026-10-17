# CPU 相关处理（src/kernel/cpu/）

本文档描述 `src/kernel/cpu/cpu.cc`、`src/kernel/cpu/acpi.cc` 以及 `src/kernel/cpu/interrupt/` 的实现。对应头文件 `include/kernel/cpu.h`、`include/kernel/acpi.h`、`include/kernel/interrupt.h`。AP 启动见 [smp.md](smp.md)。

## 1. CPU 特性检测（CpuFeatures）

```cpp
class CpuFeatures {
   public:
    void detect();
    bool hasLongMode() const;       // CPUID 0x80000001 EDX bit 29
    bool hasNoExecute() const;      // CPUID 0x80000001 EDX bit 20
    bool hasSse() const;            // CPUID leaf 1 EDX bit 25
    bool hasSse2() const;           // CPUID leaf 1 EDX bit 26
    bool hasApic() const;           // CPUID leaf 1 EDX bit 9
    bool hasX2Apic() const;         // CPUID leaf 1 ECX bit 21
    bool hasTsc() const;            // CPUID leaf 1 EDX bit 4
    bool hasInvariantTsc() const;   // CPUID 0x80000007 EDX bit 8
   private:
    bool longMode, noExecute, sse, sse2, apic, x2Apic, tsc, invariantTsc;
};
```

`cpu::init()` 要求 `longMode && noExecute && sse && sse2 && apic` 全部具备，缺一即打印 `[CPU] required CPU feature missing` 并停机。BSP 的初始 APIC ID 取自 CPUID leaf 1 的 EBX bits 31-24。

## 2. 每 CPU 数据（CpuLocalData）

```cpp
class CpuLocalData {
   public:
    constexpr CpuLocalData();       // self_ = this
    static CpuLocalData* prepare(uint32_t cpuId, uint32_t apicId);
    void initApplicationTaskorCpu();
    void initDescriptorTables(CpuArchData* arch);

    // getter/setter
    uint32_t getCpuId() const;
    uint32_t getApicId() const;
    Thread* getCurrentThread() const;
    Task* getCurrentTask() const;
    RunQueue* getRunQueue() const;
    Thread* getIdleThread() const;
    void* getKernelStack() const;    // 偏移 48（编译期断言）
    void* getIdt() const;
    bool isRescheduleRequested() const;
    uint32_t getPreemptionCount() const;
    void setKernelStack(void* stackTop);  // 同时更新 kernelStack 与 tss->rsp0
    // ... 其他 setter

    static constexpr bool hasAssemblyLayout();

   private:
    CpuLocalData* self_;            // 偏移 0，cpu::getCurrent 读 %gs:0
    uint32_t cpuId;
    uint32_t apicId;
    Thread* currentThread;
    Task* currentTask;
    RunQueue* runQueue;
    Thread* idleThread;
    void* kernelStack;              // 偏移 48（编译期断言）
    TssTable* tss;
    void* idt;
    bool rescheduleRequested;
    uint64_t tickCount;
    uint64_t syscallUserStack;      // 偏移 88（编译期断言）
    void* archData;                 // 指向 CpuArchData
    uint32_t preemptionCount;
};
```

- 通过 `IA32_GS_BASE`（MSR `0xc0000101`）定位；`cpu::getCurrent()` 用 `movq %%gs:0` 读取 `self_`。
- `kernelStack`（偏移 48）与 `syscallUserStack`（偏移 88）有 `static_assert(hasAssemblyLayout())` 保证偏移，供 `entry.S` 用 `%gs:` 固定偏移直接寻址。
- `archData` 指向每 CPU 的 `CpuArchData { TssTable tss; alignas(16) uint64_t gdt[7]; }`。

访问接口：

```cpp
namespace kernel::cpu {
    CpuLocalData* getCurrent();
    uint32_t getCurrentId();
}
```

`CpuLocalData::prepare(cpuId, apicId)` 为 AP 堆分配 `CpuLocalData` + `CpuArchData` + 32 KB 启动栈；任一失败返回 `nullptr`。

## 3. GDT 与 TSS

每 CPU 独立 GDT（7 槽）与 TSS。`initDescriptorTables` 布局：

| GDT 索引 | 选择子 | 内容 | access/flags |
|---|---|---|---|
| 0 | 0x00 | 空 | — |
| 1 | 0x08 | 内核代码 | 0x9a / 0x0a |
| 2 | 0x10 | 内核数据 | 0x92 / 0x0c |
| 3 | 0x18 | 用户数据（DPL 3） | 0xf2 / 0x0c |
| 4 | 0x20 | 用户代码（DPL 3） | 0xfa / 0x0a |
| 5-6 | 0x28 | 64 位 TSS（类型 0x89，占两槽） | — |

注意用户数据段在用户代码段之前，这是 `SYSCALL/SYSRET` 的要求（SYSRET 从 STAR 的用户基址推导段选择子，见 [syscall.md](task/syscall.md)）。

TSS（104 字节，`[[gnu::packed]]`）：

```cpp
struct TssTable {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t ioMapBase;   // = sizeof(TssTable)
};
```

- `rsp0` = 当前线程内核栈顶（由 `setKernelStack` 在每次调度切换时更新）。
- `ist1/ist2/ist3` 各分配一个 16 KB 独立栈，分别用于双错误（向量 8）、NMI（向量 2）、机器检查（向量 18），见下文 §7。

`loadGdt` 用 `lgdt` + push/lretq 远跳转重载 `CS=0x08`、重载 `DS/ES/SS=0x10`，最后 `ltr 0x28`。

## 4. Local APIC

所有函数位于 `namespace kernel::localapic`：

```cpp
namespace kernel::localapic {
void init();
void initTimer(uint8_t vector, uint32_t initialCount);
void sendEndOfInterrupt();
void sendInterTaskorInterrupt(uint32_t apicId, uint8_t vector);
void sendInitInterTaskorInterrupt(uint32_t apicId);
void sendStartupInterTaskorInterrupt(uint32_t apicId, uint8_t vector);
}
```

- `init()`：读 `IA32_APIC_BASE`（MSR 0x1b），置 bit 11（全局使能）回写；MMIO 寄存器映射为 `直接映射基址 + (base & 0xfffff000)`；写 SVR（0xF0）= `0x100 | 0xff`（APIC 使能 + 虚假中断向量 0xFF）。
- `initTimer`：分频寄存器（0x3E0）= 0x3（÷16）；LVT Timer（0x320）= `(1<<17) | vector`（配置时屏蔽）；初始计数（0x380）= initialCount。调度器用向量 32、初始计数 1,000,000（约 1 ms）。
- `sendEndOfInterrupt`：EOI 寄存器（0xB0）= 0。
- IPI 通过 ICR（0x300/0x310）发送，`writeIcr` 先等待 delivery 完成，写高位（`apicId << 24`）再写低位触发。
- `sendInitInterTaskorInterrupt`：先发 INIT assert 再发 INIT de-assert。
- `sendStartupInterTaskorInterrupt`：SIPI（`(6<<8) | vector`）。

## 5. MSR 与中断/抢占控制

```cpp
namespace kernel::cpu {
uint64_t readMsr(uint32_t msr);
void writeMsr(uint32_t msr, uint64_t value);

void disablePreemption();            // preemptionCount++
void enablePreemption();             // preemptionCount--（非零下溢保护）
bool isPreemptionEnabled();          // preemptionCount == 0
[[noreturn]] void halt();            // cli; hlt 死循环
}

namespace kernel::interrupt {
uint64_t disable();                  // pushfq; popq; cli，返回原 RFLAGS
void restore(uint64_t flags);        // 原 IF 为 1 则 sti
void enable();                       // sti
}
```

抢占计数用于保护调度器临界区：`interruptDispatch` 只在 `isPreemptionEnabled()` 时才执行尾部 `schedule()`。

## 6. ACPI 表定位（acpi.cc）

```cpp
struct [[gnu::packed]] AcpiSdtHeader {
    char signature[4]; uint32_t length; uint8_t revision; uint8_t checksum;
    char oemId[6]; char oemTableId[8]; uint32_t oemRevision;
    uint32_t creatorId; uint32_t creatorRevision;
};

namespace kernel::acpi {
const AcpiSdtHeader* findAcpiTable(const BootInfo* bootInfo, const char signature[4]);
}
```

查找流程：

1. 校验 RSDP：签名 `"RSD PTR "`（8 字节）+ 前 20 字节校验和。
2. 若 `revision >= 2` 且扩展校验和有效、`xsdtAddress != 0`：优先走 XSDT（8 字节表项）。
3. XSDT 未命中则回退 RSDT（4 字节表项）。
4. 遍历根表表项，返回第一个签名匹配且校验和有效的表；所有表都经直接映射访问，且物理地址必须 < 64 GB。

内核用 `"APIC"` 签名获取 MADT（SMP 与 I/O APIC 都依赖它）。

## 7. 中断实现（interrupt/）

### 7.1 中断上下文

`entry.S` 压栈顺序与结构体字段一一对应（22 个 `uint64_t`）：

```cpp
namespace kernel::interrupt {
struct [[gnu::packed]] Context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t errorCode;
    uint64_t rip, cs, rflags, rsp, ss;   // CPU 自动压栈部分
};
using Handler = void (*)(Context* context);
}
```

### 7.2 ISR 桩（entry.S）

- `isrNoError vector`：`pushq $0`（补假错误码）+ `pushq $vector` → `isrCommon`。
- `isrError vector`：CPU 已压错误码，仅 `pushq $vector`。
- 带真实错误码的异常向量：**8, 10, 11, 12, 13, 14, 17, 21, 29, 30**；其余 0-31 用 `isrNoError`。
- 向量 32-255 全部以 `isrNoError` 生成。
- `isrStubTable`：256 个桩地址数组，供 `initIdt` 建表。

#### `isrCommon` 公共入口

1. `cld`；依次压栈 15 个通用寄存器（rax…r15，与 `interrupt::Context` 顺序一致）。
2. 检查被中断上下文 CS 的 RPL（偏移 144）；用户态则 `swapgs`（切换到内核 per-CPU 数据）。
3. `rdi = rsp`（传 `interrupt::Context*`），栈按 16 字节对齐，`call interruptDispatch`。
4. 用户态则再次 `swapgs` 换回；逆序弹出寄存器，跳过 vector 与错误码，`iretq`。

### 7.3 IDT 管理（interrupt.cc）

所有函数位于 `namespace kernel::interrupt`：

```cpp
void initIdt();
void loadIdt();
bool registerHandler(uint8_t vector, Handler handler);
void unregisterHandler(uint8_t vector);
```

`initIdt`：

1. 全屏蔽 8259A 传统 PIC（向 0x21/0xa1 写 0xff）。
2. 填充 256 个门：段选择子固定 `0x08`，属性 `0x8e | (privilege << 5)`（Present + 64 位中断门）。
3. **IST 分配**：向量 8（#DF）→ IST 1；向量 2（NMI）→ IST 2；向量 18（#MC）→ IST 3；其余 IST 0。
4. **DPL=3 的门**：向量 3（#BP）与向量 0x80；其余 DPL 0。
5. `handlers` 全部置空；`loadIdt` 加载。

`loadIdt`：若当前 CPU 尚无 IDT，分配并从 `bootstrapIdt` 复制 256 项（**每 CPU 独立 IDT 副本**）；`lidt` 加载。BSP 初始直接使用静态 `bootstrapIdt`，AP 通过 `loadIdt` 分配副本。

`registerHandler`：加锁；handler 为空或向量已被占用返回 false；以 RELEASE 原子写。

### 7.4 中断分发

```cpp
extern "C" void interruptDispatch(interrupt::Context* context);
```

1. **向量 0x80**：系统服务快速通道——校验当前进程为 `systemService` 且来自内核态，直接调用 `syscall::dispatchSyscall`，不经过 SYSCALL 路径。
2. `vector < 32` → `handleException`。
3. 否则原子（ACQUIRE）读取处理函数并调用。
4. **向量 `0xff`（虚假中断）不发 EOI**，其余 `localapic::sendEndOfInterrupt()`。
5. 若调度器已初始化、抢占使能、且当前 CPU `rescheduleRequested` → 调用 `schedule::schedule()`（中断返回前完成调度切换）。

#### 异常处理 `handleException`

- 向量 14 → 转 `handlePageFault`（见 [memory.md](memory/memory.md)）。
- 其余先打印 vector/error/rip。
- **向量 3 且来自内核态**：直接返回（内核断点可恢复）。
- 用户态异常 → `Thread::terminateCurrentFromException(-vector)`。
- 内核态不可恢复异常 → 打印后 `cpu::halt()`。

### 7.5 I/O APIC（ioapic.cc）

```cpp
namespace kernel::interrupt {
void initControllers(const BootInfo* bootInfo);
bool setIrq(uint8_t irq, uint8_t vector, uint32_t apicId, bool levelTriggered = false);
void maskIrq(uint8_t irq);
}
```

#### 初始化

1. IRQ 0-15 默认恒等路由（`irqRoutes[irq].globalSystemInterrupt = irq, flags = 0`）。
2. 解析 MADT：
   - **type 1（I/O APIC）**：注册 MMIO 基址与 GSI 基址；重定向数量 = 寄存器 1 的 bits 16-23 + 1；最多 8 个。
   - **type 2（中断源覆盖）**：`bus == 0 且 source < 16` 时覆盖 IRQ→GSI 路由与极性/触发标志。
3. 屏蔽所有 I/O APIC 的全部引脚。
4. MADT 不可用或无 I/O APIC 时打印警告并返回（禁用外部 IRQ 路由）。

I/O APIC 寄存器经间接寻址：索引寄存器 = MMIO 偏移 0x00，数据寄存器 = 偏移 0x10。

#### IRQ 配置 `setIrq(irq, vector, apicId, levelTriggered)`

- 要求 `irq < 16` 且 `vector >= 32`。
- 按路由表找到 GSI 所属 I/O APIC 与引脚。
- 极性：`flags & 0x3 == 3`（低电平有效）或 `levelTriggered` → 重定向低字置 bit 13。
- 触发：`(flags >> 2) & 0x3 == 3`（电平触发）或 `levelTriggered` → 置 bit 15。
- 高字写目标 `apicId << 24`，低字写 `vector | 极性/触发位`（固定投递模式，未屏蔽）。

`maskIrq(irq)`：屏蔽其路由到的 GSI 引脚（置重定向低字 bit 16）。

### 7.6 向量分配汇总

| 向量 | 用途 |
|---|---|
| 0-31 | CPU 异常 |
| 32 | Local APIC Timer（调度器节拍，1 ms） |
| 64-79 | IRQ 0-15（设备中断绑定） |
| 0x80 | 系统服务快速通道（DPL 0，仅 systemService 进程内核态可用） |
| 0xf0 | SMP 重调度 IPI |
| 0xf1 | SMP TLB shootdown IPI |
| 0xff | Local APIC 虚假中断 |
