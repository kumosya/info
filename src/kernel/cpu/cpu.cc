#include "kernel/cpu.h"

#include <cstddef>
#include <new>

#include "kernel/device.h"
#include "kernel/mm.h"

namespace kernel::cpu {

namespace {

constexpr uint32_t ia32ApicBase     = 0x1b;
constexpr uint32_t ia32GsBase       = 0xc0000101;
constexpr uint32_t ia32KernelGsBase = 0xc0000102;
constexpr uint64_t directMapBase    = 0xffff800000000000ULL;
constexpr uint64_t stackSize        = 16 * 1024;
constexpr uint64_t startupStackSize = 32 * 1024;

CpuFeatures cpuFeatures{};
CpuLocalData bootstrapCpu{};
CpuArchData bootstraparch{};
volatile uint32_t* localApic;

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

void installTssDescriptor(CpuArchData* arch, uint64_t base, uint32_t limit) {
    uint64_t low = limit & 0xffff;
    low |= (base & 0xffffff) << 16;
    low |= static_cast<uint64_t>(0x89) << 40;
    low |= static_cast<uint64_t>((limit >> 16) & 0x0f) << 48;
    low |= static_cast<uint64_t>((base >> 24) & 0xff) << 56;
    arch->getGdt()[5] = low;
    arch->getGdt()[6] = base >> 32;
}

void loadGdt(CpuArchData* arch) {
    const DescriptorTablePointer pointer{
        7 * sizeof(uint64_t) - 1, reinterpret_cast<uint64_t>(arch->getGdt())};
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

void* allocateStack(uint64_t size = stackSize) {
    uint8_t* stack = static_cast<uint8_t*>(kernel::mm::allocateHeap(size));
    return stack == nullptr ? nullptr : stack + size;
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

void CpuFeatures::detect() {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    cpuid(1, 0, eax, ebx, ecx, edx);
    this->sse    = (edx & (1U << 25)) != 0;
    this->sse2   = (edx & (1U << 26)) != 0;
    this->apic   = (edx & (1U << 9)) != 0;
    this->tsc    = (edx & (1U << 4)) != 0;
    this->x2Apic = (ecx & (1U << 21)) != 0;
    bootstrapCpu.setApicId(ebx >> 24);

    cpuid(0x80000000, 0, eax, ebx, ecx, edx);
    const uint32_t maximumExtendedLeaf = eax;

    if (eax >= 0x80000001) {
        cpuid(0x80000001, 0, eax, ebx, ecx, edx);
        this->noExecute = (edx & (1U << 20)) != 0;
        this->longMode  = (edx & (1U << 29)) != 0;
    }
    if (maximumExtendedLeaf >= 0x80000007) {
        cpuid(0x80000007, 0, eax, ebx, ecx, edx);
        this->invariantTsc = (edx & (1U << 8)) != 0;
    }
}

void CpuLocalData::initDescriptorTables(CpuArchData* arch) {
    clearMemory(arch, sizeof(CpuArchData));
    arch->getGdt()[0] = 0;
    arch->getGdt()[1] = createSegmentDescriptor(0x9a, 0x0a);
    arch->getGdt()[2] = createSegmentDescriptor(0x92, 0x0c);
    arch->getGdt()[3] = createSegmentDescriptor(0xf2, 0x0c);
    arch->getGdt()[4] = createSegmentDescriptor(0xfa, 0x0a);

    arch->getTss().ioMapBase = sizeof(TssTable);
    arch->getTss().rsp0      = reinterpret_cast<uint64_t>(kernelStack);
    arch->getTss().ist1      = reinterpret_cast<uint64_t>(allocateStack());
    arch->getTss().ist2      = reinterpret_cast<uint64_t>(allocateStack());
    arch->getTss().ist3      = reinterpret_cast<uint64_t>(allocateStack());
    if (arch->getTss().rsp0 == 0 || arch->getTss().ist1 == 0 ||
        arch->getTss().ist2 == 0 || arch->getTss().ist3 == 0) {
        kernel::device::serialWrite("[CPU] unable to allocate TSS stacks\n");
        halt();
    }
    installTssDescriptor(arch, reinterpret_cast<uint64_t>(&arch->getTss()),
                         sizeof(TssTable) - 1);
    tss      = &arch->getTss();
    archData = arch;
    loadGdt(arch);
}

CpuLocalData* CpuLocalData::prepare(uint32_t cpuId, uint32_t apicId) {
    CpuLocalData* cpu = static_cast<CpuLocalData*>(
        kernel::mm::allocateHeap(sizeof(CpuLocalData)));
    CpuArchData* arch = static_cast<CpuArchData*>(
        kernel::mm::allocateHeap(sizeof(CpuArchData)));
    void* stackTop = allocateStack(startupStackSize);
    if (cpu == nullptr || arch == nullptr || stackTop == nullptr) {
        return nullptr;
    }
    new (cpu) CpuLocalData{};
    clearMemory(arch, sizeof(CpuArchData));
    cpu->setCpuId(cpuId);
    cpu->setApicId(apicId);
    cpu->setKernelStack(stackTop);
    cpu->archData = arch;
    return cpu;
}

void CpuLocalData::initApplicationTaskorCpu() {
    if (archData == nullptr) {
        cpu::halt();
    }
    cpu::writeMsr(ia32GsBase, reinterpret_cast<uint64_t>(this));
    cpu::writeMsr(ia32KernelGsBase, 0);
    initDescriptorTables(static_cast<CpuArchData*>(archData));
    cpu::initLocalApic();
}

void CpuLocalData::setKernelStack(void* stackTop) {
    // CpuLocalData* cpu = cpu::getCurrent();
    kernelStack = stackTop;
    if (tss != nullptr) tss->rsp0 = reinterpret_cast<uint64_t>(stackTop);
}

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

void init() {
    cpuFeatures.detect();
    if (!cpuFeatures.hasLongMode() || !cpuFeatures.hasNoExecute() ||
        !cpuFeatures.hasSse() || !cpuFeatures.hasSse2() ||
        !cpuFeatures.hasApic()) {
        device::serialWrite("[CPU] required CPU feature missing\n");
        halt();
    }

    // bootstrapCpu.setCpuId(0);
    bootstrapCpu.setKernelStack(allocateStack());
    bootstrapCpu.initDescriptorTables(&bootstraparch);
    cpu::writeMsr(ia32GsBase, reinterpret_cast<uint64_t>(&bootstrapCpu));
    cpu::writeMsr(ia32KernelGsBase, 0);
    cpu::initLocalApic();
    device::serialWrite("[CPU] GDT, TSS and per-CPU data initialized\n");
}

const CpuFeatures& getCpuFeatures() { return cpuFeatures; }

CpuLocalData* getCurrent() {
    CpuLocalData* cpu;
    asm volatile("movq %%gs:0, %0" : "=r"(cpu));
    return cpu;
}

uint32_t getCurrentId() { return cpu::getCurrent()->getCpuId(); }

void CpuLocalData::disablePreemption() { ++preemptionCount; }
void CpuLocalData::enablePreemption() {
    if (preemptionCount) --preemptionCount;
}
bool CpuLocalData::isPreemptionEnabled() const { return preemptionCount == 0; }
PreemptionGuard::PreemptionGuard() : PreemptionGuard(*getCurrent()) {}
void disablePreemption() { getCurrent()->disablePreemption(); }
void enablePreemption() { getCurrent()->enablePreemption(); }
bool isPreemptionEnabled() { return getCurrent()->isPreemptionEnabled(); }

[[noreturn]] void halt() {
    while (true) {
        asm volatile("cli; hlt");
    }
}

void initLocalApic() {
    uint64_t apicBase = cpu::readMsr(ia32ApicBase);
    apicBase |= 1ULL << 11;
    cpu::writeMsr(ia32ApicBase, apicBase);
    volatile uint32_t* registers = reinterpret_cast<volatile uint32_t*>(
        directMapBase + (apicBase & 0xfffff000ULL));
    if (__atomic_load_n(&localApic, __ATOMIC_ACQUIRE) == nullptr) {
        __atomic_store_n(&localApic, registers, __ATOMIC_RELEASE);
    }
    registers[0xf0 / 4] = 0x100 | 0xff;
    device::serialWrite("[CPU] Local APIC initialized\n");
}

void initTimer(uint8_t vector, uint32_t initialCount) {
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
void stopOtherCpus() {
    auto* registers = __atomic_load_n(&localApic, __ATOMIC_ACQUIRE);
    if (!registers) return;
    for (uint32_t attempt = 0; attempt < 1000000; ++attempt) {
        if (!(registers[0x300 / 4] & (1U << 12))) {
            registers[0x300 / 4] = (3U << 18) | (4U << 8);
            return;
        }
        asm volatile("pause");
    }
}

void sendInterTaskorInterrupt(uint32_t apicId, uint8_t vector) {
    writeIcr(apicId, vector);
}

void sendInitInterTaskorInterrupt(uint32_t apicId) {
    writeIcr(apicId, (5U << 8) | (1U << 14) | (1U << 15));
    writeIcr(apicId, (5U << 8) | (1U << 15));
}

void sendStartupInterTaskorInterrupt(uint32_t apicId, uint8_t vector) {
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

static_assert(CpuLocalData::hasAssemblyLayout());

}  // namespace kernel::cpu
