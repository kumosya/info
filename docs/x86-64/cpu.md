# CPU 子系统（src/kernel/cpu/）

本文档描述 `src/kernel/cpu/cpu.cc` 与 `src/kernel/cpu/acpi.cc` 的实现：CPU 特性检测、每 CPU 数据、GDT/TSS、Local APIC、中断开关与抢占计数、ACPI 表定位。对应头文件 `include/kernel/cpu.h`、`include/kernel/acpi.h`。AP 启动见 [smp.md](smp.md)。

## 1. CPU 特性检测（CpuFeatures）

```cpp
struct CpuFeatures {
    bool longMode;      // CPUID 0x80000001 EDX bit 29
    bool noExecute;     // CPUID 0x80000001 EDX bit 20
    bool sse;           // CPUID leaf 1 EDX bit 25
    bool sse2;          // CPUID leaf 1 EDX bit 26
    bool apic;          // CPUID leaf 1 EDX bit 9
    bool x2Apic;        // CPUID leaf 1 ECX bit 21
    bool tsc;           // CPUID leaf 1 EDX bit 4
    bool invariantTsc;  // CPUID 0x80000007 EDX bit 8
};
```

`initCpu()` 要求 `longMode && noExecute && sse && sse2 && apic` 全部具备，缺一即打印 `[CPU] required CPU feature missing` 并停机。BSP 的初始 APIC ID 取自 CPUID leaf 1 的 EBX bits 31-24。

## 2. 每 CPU 数据（CpuLocalData）

```cpp
struct CpuLocalData {
    CpuLocalData* self;        // 偏移 0，getCurrentCpu 读 %gs:0
    uint32_t cpuId;
    uint32_t apicId;
    Thread* currentThread;
    Process* currentProcess;
    RunQueue* runQueue;
    Thread* idleThread;
    void* kernelStack;         // 偏移 48（编译期断言）
    TaskStateSegment* tss;
    void* idt;
    bool rescheduleRequested;
    uint64_t tickCount;
    uint64_t syscallUserStack; // 偏移 88（编译期断言）
    void* architectureData;    // 指向 CpuArchitectureData
    uint32_t preemptionCount;
};
```

- 通过 `IA32_GS_BASE`（MSR `0xc0000101`）定位；`getCurrentCpu()` 用 `movq %%gs:0` 读取 `self`。
- `kernelStack`（偏移 48）与 `syscallUserStack`（偏移 88）有 `static_assert` 保证偏移，供 `entry.S` 用 `%gs:` 固定偏移直接寻址。
- `architectureData` 指向每 CPU 的 `CpuArchitectureData { TaskStateSegment tss; alignas(16) uint64_t gdt[7]; }`。

访问接口：

```cpp
CpuLocalData* getCurrentCpu();
uint32_t getCurrentCpuId();
CpuLocalData* prepareCpuLocalData(uint32_t cpuId, uint32_t apicId);  // AP 用
void initApplicationProcessorCpu(CpuLocalData* cpu);                 // AP 上调用
void setKernelStack(void* stackTop);  // 同时更新 cpu->kernelStack 与 tss->rsp0
```

`prepareCpuLocalData` 为 AP 堆分配 `CpuLocalData` + `CpuArchitectureData` + 32 KB 启动栈；任一失败返回 `nullptr`。

## 3. GDT 与 TSS

每 CPU 独立 GDT（7 槽）与 TSS。`initializeDescriptorTables` 布局：

| GDT 索引 | 选择子 | 内容 | access/flags |
|---|---|---|---|
| 0 | 0x00 | 空 | — |
| 1 | 0x08 | 内核代码 | 0x9a / 0x0a |
| 2 | 0x10 | 内核数据 | 0x92 / 0x0c |
| 3 | 0x18 | 用户数据（DPL 3） | 0xf2 / 0x0c |
| 4 | 0x20 | 用户代码（DPL 3） | 0xfa / 0x0a |
| 5-6 | 0x28 | 64 位 TSS（类型 0x89，占两槽） | — |

注意用户数据段在用户代码段之前，这是 `SYSCALL/SYSRET` 的要求（SYSRET 从 STAR 的用户基址推导段选择子，见 [syscall.md](syscall.md)）。

TSS（104 字节，`[[gnu::packed]]`）：

```cpp
struct TaskStateSegment {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t ioMapBase;   // = sizeof(TSS)
};
```

- `rsp0` = 当前线程内核栈顶（由 `setKernelStack` 在每次调度切换时更新）。
- `ist1/ist2/ist3` 各分配一个 16 KB 独立栈，分别用于双错误（向量 8）、NMI（向量 2）、机器检查（向量 18），见 [interrupt.md](interrupt.md)。

`loadGdt` 用 `lgdt` + push/lretq 远跳转重载 `CS=0x08`、重载 `DS/ES/SS=0x10`，最后 `ltr 0x28`。

## 4. Local APIC

```cpp
void initLocalApic();
void configureLocalApicTimer(uint8_t vector, uint32_t initialCount);
void sendEndOfInterrupt();
void sendInterProcessorInterrupt(uint32_t apicId, uint8_t vector);
void sendInitInterProcessorInterrupt(uint32_t apicId);
void sendStartupInterProcessorInterrupt(uint32_t apicId, uint8_t vector);
```

- `initLocalApic`：读 `IA32_APIC_BASE`（MSR 0x1b），置 bit 11（全局使能）回写；MMIO 寄存器映射为 `直接映射基址 + (base & 0xfffff000)`；写 SVR（0xF0）= `0x100 | 0xff`（APIC 使能 + 虚假中断向量 0xFF）。
- `configureLocalApicTimer`：分频寄存器（0x3E0）= 0x3（÷16）；LVT Timer（0x320）= `(1<<17) | vector`（配置时屏蔽）；初始计数（0x380）= initialCount。调度器用向量 32、初始计数 1,000,000（约 1 ms）。
- `sendEndOfInterrupt`：EOI 寄存器（0xB0）= 0。
- IPI 通过 ICR（0x300/0x310）发送，`writeIcr` 先等待 delivery 完成，写高位（`apicId << 24`）再写低位触发。
- `sendInitInterProcessorInterrupt`：先发 INIT assert 再发 INIT de-assert。
- `sendStartupInterProcessorInterrupt`：SIPI（`(6<<8) | vector`）。

## 5. MSR 与中断/抢占控制

```cpp
uint64_t readMsr(uint32_t msr);
void writeMsr(uint32_t msr, uint64_t value);

uint64_t disableInterrupts();        // pushfq; popq; cli，返回原 RFLAGS
void restoreInterrupts(uint64_t flags);  // 原 IF 为 1 则 sti
void enableInterrupts();             // sti
void disablePreemption();            // preemptionCount++
void enablePreemption();             // preemptionCount--（非零下溢保护）
bool isPreemptionEnabled();          // preemptionCount == 0
[[noreturn]] void haltCpu();         // cli; hlt 死循环
```

抢占计数用于保护调度器临界区：`interruptDispatch` 只在 `isPreemptionEnabled()` 时才执行尾部 `schedule()`。

## 6. ACPI 表定位（acpi.cc）

```cpp
struct [[gnu::packed]] AcpiSdtHeader {
    char signature[4]; uint32_t length; uint8_t revision; uint8_t checksum;
    char oemId[6]; char oemTableId[8]; uint32_t oemRevision;
    uint32_t creatorId; uint32_t creatorRevision;
};
const AcpiSdtHeader* findAcpiTable(const BootInfo* bootInfo, const char signature[4]);
```

查找流程：

1. 校验 RSDP：签名 `"RSD PTR "`（8 字节）+ 前 20 字节校验和。
2. 若 `revision >= 2` 且扩展校验和有效、`xsdtAddress != 0`：优先走 XSDT（8 字节表项）。
3. XSDT 未命中则回退 RSDT（4 字节表项）。
4. 遍历根表表项，返回第一个签名匹配且校验和有效的表；所有表都经直接映射访问，且物理地址必须 < 64 GB。

内核用 `"APIC"` 签名获取 MADT（SMP 与 I/O APIC 都依赖它）。
