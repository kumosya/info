# SMP 多核（src/kernel/cpu/smp/）

本文档描述 `src/kernel/cpu/smp/smp.cc` 与 `trampoline.S`：多核发现（MADT）、AP 启动、核间重调度与 TLB shootdown。对应头文件 `include/kernel/smp.h`。

## 1. CPU 描述符

```cpp
enum class CpuState : uint8_t { Offline, Starting, Online, Stopping, Failed };

class CpuDescriptor {
   public:
    uint32_t getCpuId() const;
    uint32_t getApicId() const;
    Atomic<CpuState>& getState();
    Atomic<uint32_t>& getStartupStage();
    CpuLocalData* getLocalData() const;
    // setter ...
   private:
    uint32_t cpuId;
    uint32_t taskorId;
    uint32_t apicId;
    bool enabled;
    Atomic<CpuState> state;
    Atomic<uint32_t startupStage>;   // AP 启动进度（诊断用）
    CpuLocalData* localData;
};
```

- 最多支持 64 个 CPU（`maximumSupportedCpus = 64`）。
- `startupStage` 含义：1 = 已发 SIPI；2 = 进入 C 入口；3 = CPU 初始化完成；4 = 调度器就绪。启动失败时串口打印该值定位卡点。

查询接口：

```cpp
namespace kernel::smp {
uint32_t getDiscoveredCpuCount();
uint32_t getOnlineCpuCount();
CpuDescriptor* getCpuDescriptor(uint32_t cpuId);
CpuLocalData* getCpuLocalData(uint32_t cpuId);
}
```

## 2. 多核发现 `smp::initSmp(const BootInfo*)`

1. `acpi::findAcpiTable(bootInfo, "APIC")` 获取 MADT。不可用则打印 `[SMP] ACPI MADT unavailable; using BSP only`，`discoveredCpuCount = 1`。
2. 统计 MADT 中 enabled 的 Local APIC（type 0）与 Local x2APIC（type 9）条目，钳制到 `[1, 64]`。
3. 分配描述符数组；描述符 0 = BSP（`CpuState::Online`，`localData = cpu::getCurrent()`）。
4. 遍历 MADT 填充 AP 描述符（初始 `Offline`）。**仅接受 `apicId <= 0xff` 的 AP**（xAPIC IPI 寻址限制）。
5. `onlineCpuCount = 1`；注册两个 IPI 处理函数（向量 0xf0 重调度、0xf1 TLB shootdown）。

## 3. AP 启动 `smp::startApplicationTaskors()`

1. 单核直接返回。
2. 校验 trampoline 代码 ≤ 4096 字节；`memory::mapPage(0x8000, 0x8000, pageWritable)` 建立低端单页恒等映射，把 `apTrampolineStart..End` 复制到物理 `0x8000`。
3. 对每个 AP：
   - `CpuLocalData::prepare`（失败置 `Failed`）；
   - 状态 `Starting`、`startupStage = 1`；
   - 向 trampoline 参数槽写入：CR3 = 内核页表根、栈顶、`CpuLocalData*`、入口 = `applicationTaskorEntry`；
   - 发送 INIT IPI → 延时 → 第一次 SIPI（向量 8 = `0x8000 >> 12`）→ 延时 → 第二次 SIPI；
   - 自旋等待状态脱离 `Starting`（超时 100,000,000 次）；未达 `Online` 则置 `Failed` 并打印 cpu/apic/stage 诊断。
4. 全部完成后 `memory::unmapPage(0x8000)`，打印在线 CPU 数。

### trampoline（trampoline.S）

复制到物理 0x8000 执行的三段跳板：

- **16 位段**：`cli; cld`，`sp = 0x7c00`，加载 trampoline 自带 GDT，CR0 置 PE，远跳 32 位入口。
- **32 位段**：段寄存器 = 0x10；CR4 置 PAE|OSFXSR|OSXMMEXCPT；从 `apStartupCr3` 槽加载 CR3；EFER 置 LME|NXE；CR0 置 PG|PE，远跳 64 位入口。
- **64 位段**：段寄存器 = 0x10；`rsp = apStartupStack`；`rdi = apStartupCpuLocal`；把 CpuLocalData 指针写入 `IA32_KERNEL_GS_BASE`；`call *apStartupEntry`。

参数槽为四个全局 `.quad` 符号：`apStartupCr3`、`apStartupStack`、`apStartupCpuLocal`、`apStartupEntry`。

### AP 的 C 入口 `applicationTaskorEntry(CpuLocalData*)`

```text
startupStage = 2
cpu->initApplicationTaskorCpu()  // GS 基址、GDT/TSS、Local APIC
startupStage = 3
interrupt::loadIdt()             // 分配每 CPU IDT 副本
syscall::init()
Thread::init()
schedule::init()
startupStage = 4
state = Online; onlineCpuCount++
Thread::exitCurrent(0)           // 进入调度（作为空闲上下文）
```

## 4. 核间重调度

```cpp
namespace kernel::smp {
void requestRemoteReschedule(uint32_t cpuId);
}
```

目标 CPU 非本机且状态 `Online` 时，发送向量 `0xf0` 的 Fixed IPI。接收方处理函数调用 `schedule::requestReschedule()`（置 `rescheduleRequested` 标志），随后在中断返回路径由 `interruptDispatch` 触发 `schedule()`。

`schedule::enqueueThread` 把线程入队到非当前 CPU 的队列时会调用本函数。

## 5. TLB shootdown

```cpp
namespace kernel::smp {
void requestTlbShootdown(uint64_t virtualAddress);
}
```

协议（一次只处理一个地址）：

1. 在线 CPU ≤ 1 直接返回。
2. 获取全局 `tlbShootdownLock` 并关中断。
3. 把地址存入 `tlbShootdownAddress`（Release），应答计数清零。
4. 向所有其他在线 CPU 发送向量 `0xf1` IPI，统计目标数。
5. 自旋等待 `tlbShootdownAcknowledgements >= targets`（pause）。
6. 恢复中断、解锁。

接收方处理函数：读地址（Acquire）→ `invlpg` → 应答计数 +1（Release）。

`memory::unmapPageInAddressSpace` 与 `memory::protectPageInAddressSpace` 在修改页表后调用本函数。

## 6. IPI 向量汇总

| 向量 | 用途 |
|---|---|
| `0xf0` | 请求目标 CPU 重调度 |
| `0xf1` | TLB shootdown（单地址） |
| `0xff` | Local APIC 虚假中断（不发 EOI） |
