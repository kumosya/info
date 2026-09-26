#include "kernel/cpu.h"

#include "kernel/heap.h"
#include "kernel/serial.h"

namespace {

constexpr uint32_t ia32ApicBase     = 0x1b;
constexpr uint32_t ia32GsBase       = 0xc0000101;
constexpr uint32_t ia32KernelGsBase = 0xc0000102;
constexpr uint64_t directMapBase    = 0xffff800000000000ULL;
constexpr uint64_t stackSize        = 16 * 1024;
constexpr uint64_t startupStackSize = 32 * 1024;

struct [[gnu::packed]] DescriptorTablePointer {
    uint16_t limit;
    uint64_t base;
};

struct CpuArchitectureData {
    kernel::TaskStateSegment tss;
    alignas(16) uint64_t gdt[7];
};

kernel::CpuFeatures cpuFeatures{};
kernel::CpuLocalData bootstrapCpu{};
CpuArchitectureData bootstrapArchitecture{};
volatile uint32_t* localApic;

static_assert(__builtin_offsetof(kernel::CpuLocalData, kernelStack) == 48);
static_assert(__builtin_offsetof(kernel::CpuLocalData, syscallUserStack) == 88);

void clearMemory(void* address, uint64_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(address);
    for (uint64_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t& eax, uint32_t& ebx,
           uint32_t& ecx, uint32_t& edx) {
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(leaf), "c"(subleaf));
}

uint64_t createSegmentDescriptor(uint8_t access, uint8_t flags) {
    return 0x000000000000ffffULL | (static_cast<uint64_t>(access) << 40) |
           (static_cast<uint64_t>(flags & 0x0f) << 52);
}

void installTssDescriptor(CpuArchitectureData* architecture, uint64_t base,
                          uint32_t limit) {
    uint64_t low = limit & 0xffff;
    low |= (base & 0xffffff) << 16;
    low |= static_cast<uint64_t>(0x89) << 40;
    low |= static_cast<uint64_t>((limit >> 16) & 0x0f) << 48;
    low |= static_cast<uint64_t>((base >> 24) & 0xff) << 56;
    architecture->gdt[5] = low;
    architecture->gdt[6] = base >> 32;
}

void loadGdt(CpuArchitectureData* architecture) {
    const DescriptorTablePointer pointer{
        sizeof(architecture->gdt) - 1,
        reinterpret_cast<uint64_t>(architecture->gdt)};
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
    const uint16_t tssSelector = 0x28;
    asm volatile("ltr %0" : : "r"(tssSelector));
}

void detectCpuFeatures() {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    cpuid(1, 0, eax, ebx, ecx, edx);
    cpuFeatures.sse     = (edx & (1U << 25)) != 0;
    cpuFeatures.sse2    = (edx & (1U << 26)) != 0;
    cpuFeatures.apic    = (edx & (1U << 9)) != 0;
    cpuFeatures.tsc     = (edx & (1U << 4)) != 0;
    cpuFeatures.x2Apic  = (ecx & (1U << 21)) != 0;
    bootstrapCpu.apicId = ebx >> 24;

    cpuid(0x80000000, 0, eax, ebx, ecx, edx);
    const uint32_t maximumExtendedLeaf = eax;
    if (eax >= 0x80000001) {
        cpuid(0x80000001, 0, eax, ebx, ecx, edx);
        cpuFeatures.noExecute = (edx & (1U << 20)) != 0;
        cpuFeatures.longMode  = (edx & (1U << 29)) != 0;
    }
    if (maximumExtendedLeaf >= 0x80000007) {
        cpuid(0x80000007, 0, eax, ebx, ecx, edx);
        cpuFeatures.invariantTsc = (edx & (1U << 8)) != 0;
    }
}

void* allocateStack(uint64_t size = stackSize) {
    uint8_t* stack = static_cast<uint8_t*>(kernel::kernelAllocate(size));
    return stack == nullptr ? nullptr : stack + size;
}

void initializeDescriptorTables(kernel::CpuLocalData* cpu,
                                CpuArchitectureData* architecture) {
    clearMemory(architecture, sizeof(CpuArchitectureData));
    architecture->gdt[0] = 0;
    architecture->gdt[1] = createSegmentDescriptor(0x9a, 0x0a);
    architecture->gdt[2] = createSegmentDescriptor(0x92, 0x0c);
    architecture->gdt[3] = createSegmentDescriptor(0xf2, 0x0c);
    architecture->gdt[4] = createSegmentDescriptor(0xfa, 0x0a);

    architecture->tss.ioMapBase = sizeof(kernel::TaskStateSegment);
    architecture->tss.rsp0      = reinterpret_cast<uint64_t>(cpu->kernelStack);
    architecture->tss.ist1      = reinterpret_cast<uint64_t>(allocateStack());
    architecture->tss.ist2      = reinterpret_cast<uint64_t>(allocateStack());
    architecture->tss.ist3      = reinterpret_cast<uint64_t>(allocateStack());
    if (architecture->tss.rsp0 == 0 || architecture->tss.ist1 == 0 ||
        architecture->tss.ist2 == 0 || architecture->tss.ist3 == 0) {
        kernel::serialWrite("[CPU] unable to allocate TSS stacks\n");
        kernel::haltCpu();
    }
    installTssDescriptor(architecture,
                         reinterpret_cast<uint64_t>(&architecture->tss),
                         sizeof(kernel::TaskStateSegment) - 1);
    cpu->tss              = &architecture->tss;
    cpu->architectureData = architecture;
    loadGdt(architecture);
}

void waitForIcrDelivery() {
    volatile uint32_t* registers =
        __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);
    while ((registers[0x300 / 4] & (1U << 12)) != 0) {
        asm volatile("pause");
    }
}

void writeIcr(uint32_t apicId, uint32_t command) {
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

}  // namespace

namespace kernel {

uint64_t readMsr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<uint64_t>(high) << 32) | low;
}

void writeMsr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr"
                 :
                 : "c"(msr), "a"(static_cast<uint32_t>(value)),
                   "d"(static_cast<uint32_t>(value >> 32)));
}

void initCpu() {
    detectCpuFeatures();
    if (!cpuFeatures.longMode || !cpuFeatures.noExecute || !cpuFeatures.sse ||
        !cpuFeatures.sse2 || !cpuFeatures.apic) {
        serialWrite("[CPU] required CPU feature missing\n");
        haltCpu();
    }

    bootstrapCpu.self        = &bootstrapCpu;
    bootstrapCpu.cpuId       = 0;
    bootstrapCpu.kernelStack = allocateStack();
    initializeDescriptorTables(&bootstrapCpu, &bootstrapArchitecture);
    writeMsr(ia32GsBase, reinterpret_cast<uint64_t>(&bootstrapCpu));
    writeMsr(ia32KernelGsBase, 0);
    initLocalApic();
    serialWrite("[CPU] GDT, TSS and per-CPU data initialized\n");
}

CpuLocalData* prepareCpuLocalData(uint32_t cpuId, uint32_t apicId) {
    CpuLocalData* cpu =
        static_cast<CpuLocalData*>(kernelAllocate(sizeof(CpuLocalData)));
    CpuArchitectureData* architecture = static_cast<CpuArchitectureData*>(
        kernelAllocate(sizeof(CpuArchitectureData)));
    void* stackTop = allocateStack(startupStackSize);
    if (cpu == nullptr || architecture == nullptr || stackTop == nullptr) {
        return nullptr;
    }
    clearMemory(cpu, sizeof(CpuLocalData));
    clearMemory(architecture, sizeof(CpuArchitectureData));
    cpu->self             = cpu;
    cpu->cpuId            = cpuId;
    cpu->apicId           = apicId;
    cpu->kernelStack      = stackTop;
    cpu->architectureData = architecture;
    return cpu;
}

void initApplicationProcessorCpu(CpuLocalData* cpu) {
    if (cpu == nullptr || cpu->architectureData == nullptr) {
        haltCpu();
    }
    writeMsr(ia32GsBase, reinterpret_cast<uint64_t>(cpu));
    writeMsr(ia32KernelGsBase, 0);
    initializeDescriptorTables(
        cpu, static_cast<CpuArchitectureData*>(cpu->architectureData));
    initLocalApic();
}

const CpuFeatures& getCpuFeatures() { return cpuFeatures; }

CpuLocalData* getCurrentCpu() {
    CpuLocalData* cpu;
    asm volatile("movq %%gs:0, %0" : "=r"(cpu));
    return cpu;
}

uint32_t getCurrentCpuId() { return getCurrentCpu()->cpuId; }

void setKernelStack(void* stackTop) {
    CpuLocalData* cpu = getCurrentCpu();
    cpu->kernelStack  = stackTop;
    cpu->tss->rsp0    = reinterpret_cast<uint64_t>(stackTop);
}

void initLocalApic() {
    uint64_t apicBase = readMsr(ia32ApicBase);
    apicBase |= 1ULL << 11;
    writeMsr(ia32ApicBase, apicBase);
    volatile uint32_t* registers = reinterpret_cast<volatile uint32_t*>(
        directMapBase + (apicBase & 0xfffff000ULL));
    if (__atomic_load_n(&localApic, __ATOMIC_ACQUIRE) == nullptr) {
        __atomic_store_n(&localApic, registers, __ATOMIC_RELEASE);
    }
    registers[0xf0 / 4] = 0x100 | 0xff;
    serialWrite("[CPU] Local APIC initialized\n");
}

void configureLocalApicTimer(uint8_t vector, uint32_t initialCount) {
    volatile uint32_t* registers =
        __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);
    registers[0x3e0 / 4] = 0x3;
    registers[0x320 / 4] = (1U << 17) | vector;
    registers[0x380 / 4] = initialCount;
}

void sendEndOfInterrupt() {
    volatile uint32_t* registers =
        __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);
    registers[0xb0 / 4] = 0;
}

void sendInterProcessorInterrupt(uint32_t apicId, uint8_t vector) {
    writeIcr(apicId, vector);
}

void sendInitInterProcessorInterrupt(uint32_t apicId) {
    writeIcr(apicId, (5U << 8) | (1U << 14) | (1U << 15));
    writeIcr(apicId, (5U << 8) | (1U << 15));
}

void sendStartupInterProcessorInterrupt(uint32_t apicId, uint8_t vector) {
    writeIcr(apicId, (6U << 8) | vector);
}

uint64_t disableInterrupts() {
    uint64_t flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void restoreInterrupts(uint64_t flags) {
    if ((flags & (1ULL << 9)) != 0) {
        asm volatile("sti" : : : "memory");
    }
}

void enableInterrupts() { asm volatile("sti" : : : "memory"); }

void disablePreemption() { ++getCurrentCpu()->preemptionCount; }

void enablePreemption() {
    CpuLocalData* cpu = getCurrentCpu();
    if (cpu->preemptionCount != 0) {
        --cpu->preemptionCount;
    }
}

bool isPreemptionEnabled() { return getCurrentCpu()->preemptionCount == 0; }

[[noreturn]] void haltCpu() {
    while (true) {
        asm volatile("cli; hlt");
    }
}

}  // namespace kernel
