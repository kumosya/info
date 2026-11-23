/*
 * 准备 CPU 描述符、局部状态、APIC 中断与定时器。
 */

#include "kernel/cpu.h"

#include <cstddef>
#include <new>

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/time.h"

namespace cpu {

    constexpr uint32_t ia32ApicBase     = 0x1b;
    constexpr uint32_t ia32GsBase       = 0xc0000101;
    constexpr uint32_t ia32KernelGsBase = 0xc0000102;
    constexpr uint64_t directMapBase    = 0xffff800000000000ULL;
    constexpr uint64_t stackSize        = 16 * 1024;
    constexpr uint64_t startupStackSize = 32 * 1024;

    static Features cpuFeatures{};
    static LocalData bootstrapCpu{};
    static ArchData bootstraparch{};
    static volatile uint32_t* localApic;

    /*
     * 按字节清零 CPU 管理存储，避免描述符和状态字段沿用堆内旧数据。
     */
    static void clearMemory(void* address, uint64_t size) {
        uint8_t* bytes = static_cast<uint8_t*>(address);

        for (uint64_t index = 0; index < size; ++index) {
            bytes[index] = 0;
        }
    }

    /*
     * 执行 CPUID 查询，把指定功能页的四个寄存器结果分别返回。
     */
    static void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& eax,
                      uint32_t& ebx, uint32_t& ecx, uint32_t& edx) {
        asm volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(leaf), "c"(subleaf));
    }

    /*
     * 按 x86 描述符位布局组合访问属性和标志，供内核及用户代码、数据段使用。
     */
    static uint64_t createSegmentDescriptor(uint8_t access, uint8_t flags) {
        return 0x000000000000ffffULL | (static_cast<uint64_t>(access) << 40) |
               (static_cast<uint64_t>(flags & 0x0f) << 52);
    }

    /*
     * 把 64 位 TSS 地址和长度写成占两个 GDT 槽的系统描述符，供 CPU 找到特权栈。
     */
    static void installTssDescriptor(ArchData* arch, uint64_t base,
                                     uint32_t limit) {
        // TSS
        // 描述符的地址和长度被硬件分散存放；先拼低槽，再把地址高位写入相邻槽。
        uint64_t low = limit & 0xffff;
        low |= (base & 0xffffff) << 16;
        low |= static_cast<uint64_t>(0x89) << 40;
        low |= static_cast<uint64_t>((limit >> 16) & 0x0f) << 48;
        low |= static_cast<uint64_t>((base >> 24) & 0xff) << 56;
        arch->getGdt()[5] = low;
        arch->getGdt()[6] = base >> 32;
    }

    /*
     * 装入当前 CPU 的 GDT，通过远返回刷新代码段，再装入数据段和 TSS 选择子。
     */
    static void loadGdt(ArchData* arch) {
        const DescriptorTablePointer pointer{
            7 * sizeof(uint64_t) - 1,
            reinterpret_cast<uint64_t>(arch->getGdt())};

        // LGDT 只更换表的位置，不会刷新已有段寄存器；下面用远返回重装
        // CS，并重新设置数据段。
        asm volatile("lgdt %0" : : "m"(pointer));
        asm volatile(
            "pushq $0x08\n"
            "leaq 1f(%%rip), %%rax\n"
            "pushq %%rax\n"
            "lretq\n"
            "1:\n"
            "movw $0x10, %%ax\n"
            "movw %%ax, %%ds\n"
            "movw %%ax, %%es\n"
            "movw %%ax, %%ss\n"
            :
            :
            : "rax", "memory");

        // TSS 位于第五个 GDT 槽，选择子为 0x28；LTR 让硬件开始使用本 CPU
        // 的特权栈信息。
        const uint16_t tssSelector = 0x28;
        asm volatile("ltr %0" : : "r"(tssSelector));
    }

    /*
     * 申请一段内核栈并返回栈顶，匹配 x86 栈从高地址向低地址增长的规则。
     */
    static void* allocateStack(uint64_t size = stackSize) {
        uint8_t* stack = static_cast<uint8_t*>(mm::allocateHeap(size));

        return stack == nullptr ? nullptr : stack + size;
    }

    /*
     * 等待本地 APIC 上一次跨核中断发送结束，防止新命令覆盖仍在发送的寄存器。
     */
    static void waitForIcrDelivery() {
        volatile uint32_t* registers =
            __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);

        while ((registers[0x300 / 4] & (1U << 12)) != 0) {
            asm volatile("pause");
        }
    }

    /*
     * 向指定 APIC 发送命令。
     * 先等待发送空闲，写目标和命令，再等待本次硬件发送结束。
     */
    static void writeIcr(uint32_t apicId, uint32_t command) {
        volatile uint32_t* registers =
            __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);

        if (registers == nullptr) {
            return;
        }

        waitForIcrDelivery();
        registers[0x310 / 4] = apicId << 24;
        registers[0x300 / 4] = command;
        waitForIcrDelivery();
    }

    /*
     * 查询处理器基本和扩展功能，记录内核需要的长模式、不可执行页、浮点与 APIC
     * 等能力。
     */
    void Features::detect() {
        uint32_t eax;
        uint32_t ebx;
        uint32_t ecx;
        uint32_t edx;

        // 基本功能页提供浮点、计时和中断能力，也能取得引导 CPU 的 APIC 编号。
        cpuid(1, 0, eax, ebx, ecx, edx);
        this->sse    = (edx & (1U << 25)) != 0;
        this->sse2   = (edx & (1U << 26)) != 0;
        this->apic   = (edx & (1U << 9)) != 0;
        this->tsc    = (edx & (1U << 4)) != 0;
        this->x2Apic = (ecx & (1U << 21)) != 0;
        bootstrapCpu.setApicId(ebx >> 24);

        // 先询问扩展页上限，只有硬件声明存在的页才能继续查询。
        cpuid(0x80000000, 0, eax, ebx, ecx, edx);
        const uint32_t maximumExtendedLeaf = eax;

        if (eax >= 0x80000001) {
            cpuid(0x80000001, 0, eax, ebx, ecx, edx);
            this->noExecute = (edx & (1U << 20)) != 0;
            this->longMode  = (edx & (1U << 29)) != 0;
        }

        // 恒定 TSC 表示计数速率不随节能频率变化；时钟初始化会据此输出能力说明。
        if (maximumExtendedLeaf >= 0x80000007) {
            cpuid(0x80000007, 0, eax, ebx, ecx, edx);
            this->invariantTsc = (edx & (1U << 8)) != 0;
        }
    }

    /*
     * 为本 CPU 建立 GDT、TSS 和严重异常使用的独立栈，最后加载这些描述符。
     */
    void LocalData::initDescriptorTables(ArchData* arch) {
        // INIT 后副核的 CD/NW 位仍可能禁用缓存；在加入调度前统一开启。
        uint64_t control;
        asm volatile("mov %%cr0, %0" : "=r"(control));
        if (control & ((1ULL << 30) | (1ULL << 29))) {
            control &= ~((1ULL << 30) | (1ULL << 29));
            asm volatile("wbinvd; mov %0, %%cr0" : : "r"(control) : "memory");
        }

        // 每个 CPU 使用相同的 PAT 第 1 项表示合并写入显存，其他项保持原值。
        uint32_t eax, ebx, ecx, edx;
        cpuid(1, 0, eax, ebx, ecx, edx);
        if (edx & (1U << 16)) {
            const uint64_t previous = readMsr(0x277);
            writeMsr(0x277, (previous & ~(0xffULL << 8)) | (1ULL << 8));
        }

        clearMemory(arch, sizeof(ArchData));
        arch->getGdt()[0] = 0;

        // 前四个有效槽分别供内核代码、内核数据、用户数据、用户代码使用；权限位决定哪些执行级别可以加载它们。
        arch->getGdt()[1] = createSegmentDescriptor(0x9a, 0x0a);
        arch->getGdt()[2] = createSegmentDescriptor(0x92, 0x0c);
        arch->getGdt()[3] = createSegmentDescriptor(0xf2, 0x0c);
        arch->getGdt()[4] = createSegmentDescriptor(0xfa, 0x0a);

        // rsp0 用于用户态进入内核；三个 IST
        // 栈供严重异常使用，即使原来的栈损坏也有独立落脚点。
        arch->getTss().ioMapBase = sizeof(TssTable);
        arch->getTss().rsp0      = reinterpret_cast<uint64_t>(kernelStack);
        arch->getTss().ist1      = reinterpret_cast<uint64_t>(allocateStack());
        arch->getTss().ist2      = reinterpret_cast<uint64_t>(allocateStack());
        arch->getTss().ist3      = reinterpret_cast<uint64_t>(allocateStack());

        if (arch->getTss().rsp0 == 0 || arch->getTss().ist1 == 0 ||
            arch->getTss().ist2 == 0 || arch->getTss().ist3 == 0) {
            device::serialWrite("[CPU] unable to allocate TSS stacks\n");
            halt();
        }

        // 栈都就绪后才发布 TSS
        // 描述符并装入硬件，避免异常入口使用尚未准备好的栈。
        installTssDescriptor(arch, reinterpret_cast<uint64_t>(&arch->getTss()),
                             sizeof(TssTable) - 1);
        tss      = &arch->getTss();
        archData = arch;
        loadGdt(arch);
    }

    /*
     * 为一个尚未启动的 CPU 准备局部状态、架构数据和初始内核栈；后续由该 CPU
     * 自己完成寄存器初始化。
     */
    LocalData* LocalData::prepare(uint32_t cpuId, uint32_t apicId) {
        // 引导 CPU 代新 CPU
        // 分配局部状态、描述符存储和启动栈；此时还不能切换当前 CPU 的寄存器。
        LocalData* cpu =
            static_cast<LocalData*>(mm::allocateHeap(sizeof(LocalData)));
        ArchData* arch =
            static_cast<ArchData*>(mm::allocateHeap(sizeof(ArchData)));
        void* stackTop = allocateStack(startupStackSize);

        if (cpu == nullptr || arch == nullptr || stackTop == nullptr) {
            return nullptr;
        }

        // 在已分配存储上构造对象，再记录两个不同的编号：内核连续编号和硬件 APIC
        // 编号。
        new (cpu) LocalData{};
        clearMemory(arch, sizeof(ArchData));
        cpu->setCpuId(cpuId);
        cpu->setApicId(apicId);
        cpu->setKernelStack(stackTop);
        cpu->archData = arch;

        return cpu;
    }

    /*
     * 在新启动 CPU 上设置 GS 局部数据地址，加载自己的描述符表并启用本地 APIC。
     */
    void LocalData::initApplicationTaskorCpu() {
        if (archData == nullptr) {
            cpu::halt();
        }

        // GS 指向本 CPU 的局部状态，必须先设置它，再让中断和调度代码通过
        // getCurrent 访问。
        cpu::writeMsr(ia32GsBase, reinterpret_cast<uint64_t>(this));
        cpu::writeMsr(ia32KernelGsBase, 0);
        initDescriptorTables(static_cast<ArchData*>(archData));
        cpu::initLocalApic();
    }

    /*
     * 更新当前线程的内核栈顶，同时更新 TSS 的用户态进入内核时使用的栈地址。
     */
    void LocalData::setKernelStack(void* stackTop) {
        // LocalData* cpu = cpu::getCurrent();
        kernelStack = stackTop;

        if (tss != nullptr) {
            tss->rsp0 = reinterpret_cast<uint64_t>(stackTop);
        }
    }

    /*
     * 读取指定模型专用寄存器，把高低两个 32 位结果组合成 64 位值。
     */
    uint64_t readMsr(uint32_t msr) {
        uint32_t low;
        uint32_t high;
        asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));

        return (static_cast<uint64_t>(high) << 32) | low;
    }

    /*
     * 把 64 位数拆成高低两部分，写入指定模型专用寄存器。
     */
    void writeMsr(uint32_t msr, uint64_t value) {
        asm volatile("wrmsr"
                     :
                     : "c"(msr), "a"(static_cast<uint32_t>(value)),
                       "d"(static_cast<uint32_t>(value >> 32)));
    }

    /*
     * 检测必要 CPU 功能，准备引导 CPU 的内核栈与描述符表，再建立 GS 局部数据和
     * APIC 状态。
     */
    void init() {
        // 这些功能直接影响页权限、浮点保存和中断处理；缺少必要能力时停止，不能带着不完整配置运行。
        cpuFeatures.detect();

        if (!cpuFeatures.hasLongMode() || !cpuFeatures.hasNoExecute() ||
            !cpuFeatures.hasSse() || !cpuFeatures.hasSse2() ||
            !cpuFeatures.hasApic()) {
            device::serialWrite("[CPU] required CPU feature missing\n");
            halt();
        }

        // bootstrapCpu.setCpuId(0);

        // 先建立引导 CPU 的栈和描述符，再设置 GS，最后启用本地中断控制器。
        bootstrapCpu.setKernelStack(allocateStack());
        bootstrapCpu.initDescriptorTables(&bootstraparch);
        cpu::writeMsr(ia32GsBase, reinterpret_cast<uint64_t>(&bootstrapCpu));
        cpu::writeMsr(ia32KernelGsBase, 0);
        cpu::initLocalApic();
        device::serialWrite("[CPU] GDT, TSS and per-CPU data initialized\n");
    }

    /*
     * 返回启动时检测得到的 CPU 功能集合，不重新执行硬件检测。
     */
    const Features& getFeatures() { return cpuFeatures; }

    /* 查询本 CPU 已配置的写合并类型，缺少 PAT 时沿用禁缓存显存映射。 */
    bool hasWriteCombining() {
        uint32_t eax, ebx, ecx, edx;
        cpuid(1, 0, eax, ebx, ecx, edx);
        return (edx & (1U << 16)) && ((readMsr(0x277) >> 8) & 0xff) == 1;
    }

    /*
     * 从 GS 指向的 CPU 局部结构取出自身指针，使每个 CPU 得到自己的当前状态。
     */
    LocalData* getCurrent() {
        LocalData* cpu;
        asm volatile("movq %%gs:0, %0" : "=r"(cpu));

        return cpu;
    }

    /*
     * 读取当前 CPU 的内核编号，这个编号与硬件 APIC 编号分别管理。
     */
    uint32_t getCurrentId() { return cpu::getCurrent()->getCpuId(); }

    /*
     * 增加本 CPU 的禁止抢占层数，支持多个内层操作嵌套保护。
     */
    void LocalData::disablePreemption() { ++preemptionCount; }

    /*
     * 归还一层禁止抢占计数；仍有外层保护时保持不可抢占。
     */
    void LocalData::enablePreemption() {
        if (preemptionCount) {
            --preemptionCount;
        }
    }

    /*
     * 只有禁止抢占计数归零才允许调度器抢占当前执行流。
     */
    bool LocalData::isPreemptionEnabled() const {
        return preemptionCount == 0;
    }

    /*
     * 构造保护对象时绑定当前 CPU，作用域结束时由其成员规则恢复抢占状态。
     */
    PreemptionGuard::PreemptionGuard() : PreemptionGuard(*getCurrent()) {}

    /*
     * 通过当前 CPU 局部状态增加一层抢占保护。
     */
    void disablePreemption() { getCurrent()->disablePreemption(); }

    /*
     * 通过当前 CPU 局部状态归还一层抢占保护。
     */
    void enablePreemption() { getCurrent()->enablePreemption(); }

    /*
     * 查询当前 CPU 是否允许发生调度抢占。
     */
    bool isPreemptionEnabled() { return getCurrent()->isPreemptionEnabled(); }

    /*
     * 关闭本地可屏蔽中断并永久停机，不再返回调用者。
     */
    [[noreturn]] void halt() {
        while (true) {
            asm volatile("cli; hlt");
        }
    }

    /*
     * 启用本地 APIC 并登记寄存器访问地址，设置伪中断向量，使本 CPU 可以接收
     * APIC 中断。
     */
    void initLocalApic() {
        // 打开 APIC
        // 的硬件使能位，并把物理寄存器地址换成内核可访问的直接映射地址。
        uint64_t apicBase = cpu::readMsr(ia32ApicBase);
        apicBase |= 1ULL << 11;
        cpu::writeMsr(ia32ApicBase, apicBase);
        volatile uint32_t* registers = reinterpret_cast<volatile uint32_t*>(
            directMapBase + (apicBase & 0xfffff000ULL));

        if (__atomic_load_n(&localApic, __ATOMIC_ACQUIRE) == nullptr) {
            __atomic_store_n(&localApic, registers, __ATOMIC_RELEASE);
        }

        // 软件使能位与伪中断向量一起设置；0xff 向量不需要普通中断结束确认。
        registers[0xf0 / 4] = 0x100 | 0xff;
        device::serialWrite("[CPU] Local APIC initialized\n");
    }

    /*
     * 用已初始化的单调时钟标定本 CPU 的 APIC
     * 计数频率，再按请求周期启动周期中断。
     */
    void initTimer(uint8_t vector, uint32_t intervalMicroseconds) {
        volatile uint32_t* registers =
            __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);
        // APIC 计数不是纳秒，需要用已初始化的单调时钟分别标定每个 CPU
        // 的定时器。

        // 标定期间关闭本地中断，把定时器设为屏蔽状态并从最大值倒数，避免中途触发调度。
        const uint64_t flags = cpu::disableInterrupts();
        registers[0x3e0 / 4] = 0x3;
        registers[0x320 / 4] = (1U << 16) | vector;
        registers[0x380 / 4] = UINT32_MAX;

        // 用已初始化的单调时钟等待至少五毫秒，测量同一段时间内 APIC
        // 实际减少的计数。
        const uint64_t begin = time::getMonotonicTime();
        uint64_t elapsed;
        do {
            asm volatile("pause");
            elapsed = time::getMonotonicTime() - begin;
        } while (elapsed < 5000000);
        const uint64_t ticks = UINT32_MAX - registers[0x390 / 4];
        registers[0x380 / 4] = 0;

        // 将测得的计数速度换算成请求周期；计数不能为零，也不能超过硬件的 32
        // 位容量。
        const uint64_t count = ticks * intervalMicroseconds * 1000ULL / elapsed;

        if (!count || count > UINT32_MAX) {
            device::serialWrite("[CPU] APIC timer calibration failed\n");
            cpu::halt();
        }

        // 标定成功才改成周期模式并装入起始计数，随后恢复原来的中断允许状态。
        registers[0x320 / 4] = (1U << 17) | vector;
        registers[0x380 / 4] = uint32_t(count);
        cpu::restoreInterrupts(flags);
    }

    /*
     * 向本地 APIC 写入中断结束确认，使它可以继续接受后续硬件中断。
     */
    void sendEndOfInterrupt() {
        volatile uint32_t* registers =
            __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);
        registers[0xb0 / 4] = 0;
    }

    /*
     * 在不可恢复错误时尽力向其他 CPU
     * 广播停止通知；等待发送器有次数上限，避免错误输出路径无限等待。
     */
    void stopOtherCpus() {
        volatile uint32_t* registers =
            __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);

        if (!registers) {
            return;
        }

        for (uint32_t attempt = 0; attempt < 1000000; ++attempt) {
            if (!(registers[0x300 / 4] & (1U << 12))) {
                registers[0x300 / 4] = (3U << 18) | (4U << 8);

                return;
            }

            asm volatile("pause");
        }
    }

    /*
     * 向指定 CPU 发送固定向量的跨核中断，用于重调度和页表缓存更新。
     */
    void sendInterTaskorInterrupt(uint32_t apicId, uint8_t vector) {
        writeIcr(apicId, vector);
    }

    /*
     * 发送 INIT 断言与撤销命令，让目标 CPU 回到接受启动向量的状态。
     */
    void sendInitInterTaskorInterrupt(uint32_t apicId) {
        writeIcr(apicId, (5U << 8) | (1U << 14) | (1U << 15));
        writeIcr(apicId, (5U << 8) | (1U << 15));
    }

    /*
     * 发送启动中断，向量指明目标 CPU 应从哪个低地址物理页开始执行。
     */
    void sendStartupInterTaskorInterrupt(uint32_t apicId, uint8_t vector) {
        writeIcr(apicId, (6U << 8) | vector);
    }

    /*
     * 保存本地标志寄存器并关闭可屏蔽中断，返回值用于之后恢复原中断允许状态。
     */
    uint64_t disableInterrupts() {
        uint64_t flags;
        asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");

        return flags;
    }

    /*
     * 原状态允许中断时重新打开中断；原状态关闭时保持调用方当前的关闭状态。
     */
    void restoreInterrupts(uint64_t flags) {
        if ((flags & (1ULL << 9)) != 0) {
            asm volatile("sti" : : : "memory");
        }
    }

    /*
     * 显式打开本地可屏蔽中断，调用者应已完成中断所需的栈和描述符准备。
     */
    void enableInterrupts() { asm volatile("sti" : : : "memory"); }

    static_assert(LocalData::hasAssemblyLayout());

}  // namespace cpu
