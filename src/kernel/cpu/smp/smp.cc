#include "kernel/smp.h"

#include "kernel/acpi.h"
#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/heap.h"
#include "kernel/interrupt.h"
#include "kernel/page.h"
#include "kernel/scheduler.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/syscall.h"
#include "kernel/thread.h"

namespace {

constexpr uint32_t maximumSupportedCpus = 64;
constexpr uint64_t directMapBase        = 0xffff800000000000ULL;
constexpr uint64_t trampolineAddress    = 0x8000;
constexpr uint8_t trampolineVector      = trampolineAddress >> 12;

struct [[gnu::packed]] Madt {
    kernel::AcpiSdtHeader header;
    uint32_t localApicAddress;
    uint32_t flags;
};

struct [[gnu::packed]] MadtEntryHeader {
    uint8_t type;
    uint8_t length;
};

struct [[gnu::packed]] MadtLocalApic {
    MadtEntryHeader header;
    uint8_t processorId;
    uint8_t apicId;
    uint32_t flags;
};

struct [[gnu::packed]] MadtLocalX2Apic {
    MadtEntryHeader header;
    uint16_t reserved;
    uint32_t x2ApicId;
    uint32_t flags;
    uint32_t processorId;
};

kernel::CpuDescriptor* cpuDescriptors;
uint32_t discoveredCpuCount;
kernel::Atomic<uint32_t> onlineCpuCount{0};
kernel::SpinLock tlbShootdownLock;
kernel::Atomic<uint64_t> tlbShootdownAddress{0};
kernel::Atomic<uint32_t> tlbShootdownAcknowledgements{0};

extern "C" uint8_t apTrampolineStart;
extern "C" uint8_t apTrampolineEnd;
extern "C" uint64_t apStartupCr3;
extern "C" uint64_t apStartupStack;
extern "C" uint64_t apStartupCpuLocal;
extern "C" uint64_t apStartupEntry;

void clearMemory(void* address, uint64_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(address);
    for (uint64_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

template <typename Type>
Type* trampolinePointer(Type* symbol) {
    const uint64_t offset = reinterpret_cast<uint64_t>(symbol) -
                            reinterpret_cast<uint64_t>(&apTrampolineStart);
    return reinterpret_cast<Type*>(directMapBase + trampolineAddress + offset);
}

void startupDelay(uint32_t iterations) {
    for (uint32_t index = 0; index < iterations; ++index) {
        asm volatile("pause");
    }
}

bool processorEnabled(uint32_t flags) { return (flags & 1U) != 0; }

uint32_t countEnabledProcessors(const Madt* madt) {
    uint32_t count = 0;
    const uint8_t* cursor =
        reinterpret_cast<const uint8_t*>(madt) + sizeof(Madt);
    const uint8_t* end =
        reinterpret_cast<const uint8_t*>(madt) + madt->header.length;
    while (cursor + sizeof(MadtEntryHeader) <= end) {
        const MadtEntryHeader* header =
            reinterpret_cast<const MadtEntryHeader*>(cursor);
        if (header->length < sizeof(MadtEntryHeader) ||
            cursor + header->length > end) {
            break;
        }
        if (header->type == 0 && header->length >= sizeof(MadtLocalApic) &&
            processorEnabled(
                reinterpret_cast<const MadtLocalApic*>(cursor)->flags)) {
            ++count;
        } else if (header->type == 9 &&
                   header->length >= sizeof(MadtLocalX2Apic) &&
                   processorEnabled(
                       reinterpret_cast<const MadtLocalX2Apic*>(cursor)
                           ->flags)) {
            ++count;
        }
        cursor += header->length;
    }
    return count;
}

void initializeDescriptor(kernel::CpuDescriptor& descriptor, uint32_t cpuId,
                          uint32_t processorId, uint32_t apicId,
                          kernel::CpuState state) {
    descriptor.cpuId       = cpuId;
    descriptor.processorId = processorId;
    descriptor.apicId      = apicId;
    descriptor.enabled     = true;
    descriptor.localData   = nullptr;
    descriptor.state.store(state, kernel::MemoryOrder::Relaxed);
    descriptor.startupStage.store(0, kernel::MemoryOrder::Relaxed);
}

void rescheduleIpiHandler(kernel::InterruptContext*) {
    kernel::requestReschedule();
}

void tlbShootdownIpiHandler(kernel::InterruptContext*) {
    const uint64_t address =
        tlbShootdownAddress.load(kernel::MemoryOrder::Acquire);
    asm volatile("invlpg (%0)" : : "r"(address) : "memory");
    tlbShootdownAcknowledgements.fetchAdd(1, kernel::MemoryOrder::Release);
}

}  // namespace

namespace kernel {

void initSmp(const BootInfo* bootInfo) {
    const Madt* madt =
        reinterpret_cast<const Madt*>(findAcpiTable(bootInfo, "APIC"));
    if (madt == nullptr || madt->header.length < sizeof(Madt)) {
        serialWrite("[SMP] ACPI MADT unavailable; using BSP only\n");
        discoveredCpuCount = 1;
    } else {
        discoveredCpuCount = countEnabledProcessors(madt);
        if (discoveredCpuCount == 0) {
            discoveredCpuCount = 1;
        } else if (discoveredCpuCount > maximumSupportedCpus) {
            discoveredCpuCount = maximumSupportedCpus;
        }
    }

    cpuDescriptors = static_cast<CpuDescriptor*>(
        kernelAllocate(sizeof(CpuDescriptor) * discoveredCpuCount));
    if (cpuDescriptors == nullptr) {
        serialWrite("[SMP] unable to allocate CPU descriptors\n");
        haltCpu();
    }
    clearMemory(cpuDescriptors, sizeof(CpuDescriptor) * discoveredCpuCount);
    initializeDescriptor(cpuDescriptors[0], 0, 0, getCurrentCpu()->apicId,
                         CpuState::Online);
    cpuDescriptors[0].localData = getCurrentCpu();

    uint32_t nextCpuId = 1;
    if (madt != nullptr) {
        const uint8_t* cursor =
            reinterpret_cast<const uint8_t*>(madt) + sizeof(Madt);
        const uint8_t* end =
            reinterpret_cast<const uint8_t*>(madt) + madt->header.length;
        while (cursor + sizeof(MadtEntryHeader) <= end &&
               nextCpuId < discoveredCpuCount) {
            const MadtEntryHeader* header =
                reinterpret_cast<const MadtEntryHeader*>(cursor);
            if (header->length < sizeof(MadtEntryHeader) ||
                cursor + header->length > end) {
                break;
            }
            uint32_t processorId = 0;
            uint32_t apicId      = 0xffffffffU;
            uint32_t flags       = 0;
            if (header->type == 0 && header->length >= sizeof(MadtLocalApic)) {
                const MadtLocalApic* entry =
                    reinterpret_cast<const MadtLocalApic*>(cursor);
                processorId = entry->processorId;
                apicId      = entry->apicId;
                flags       = entry->flags;
            } else if (header->type == 9 &&
                       header->length >= sizeof(MadtLocalX2Apic)) {
                const MadtLocalX2Apic* entry =
                    reinterpret_cast<const MadtLocalX2Apic*>(cursor);
                processorId = entry->processorId;
                apicId      = entry->x2ApicId;
                flags       = entry->flags;
            }
            if (processorEnabled(flags)) {
                if (apicId == getCurrentCpu()->apicId) {
                    cpuDescriptors[0].processorId = processorId;
                } else if (apicId <= 0xff) {
                    initializeDescriptor(cpuDescriptors[nextCpuId], nextCpuId,
                                         processorId, apicId,
                                         CpuState::Offline);
                    ++nextCpuId;
                }
            }
            cursor += header->length;
        }
    }
    discoveredCpuCount = nextCpuId == 0 ? 1 : nextCpuId;
    onlineCpuCount.store(1, MemoryOrder::Release);
    registerInterruptHandler(rescheduleIpiVector, rescheduleIpiHandler);
    registerInterruptHandler(tlbShootdownIpiVector, tlbShootdownIpiHandler);
    serialWrite("[SMP] discovered CPUs=");
    serialWriteDecimal(discoveredCpuCount);
    serialWrite(" BSP APIC ID=");
    serialWriteDecimal(getCurrentCpu()->apicId);
    serialWrite("\n");
}

void startApplicationProcessors() {
    if (discoveredCpuCount <= 1) {
        serialWrite("[SMP] no application processors to start\n");
        return;
    }
    const uint64_t trampolineSize =
        reinterpret_cast<uint64_t>(&apTrampolineEnd) -
        reinterpret_cast<uint64_t>(&apTrampolineStart);
    if (trampolineSize > 4096 ||
        !mapPage(trampolineAddress, trampolineAddress, pageWritable)) {
        serialWrite("[SMP] unable to map AP trampoline\n");
        return;
    }
    uint8_t* trampoline =
        reinterpret_cast<uint8_t*>(directMapBase + trampolineAddress);
    const uint8_t* source = &apTrampolineStart;
    for (uint64_t index = 0; index < trampolineSize; ++index) {
        trampoline[index] = source[index];
    }

    for (uint32_t cpuId = 1; cpuId < discoveredCpuCount; ++cpuId) {
        CpuDescriptor& descriptor = cpuDescriptors[cpuId];
        descriptor.localData = prepareCpuLocalData(cpuId, descriptor.apicId);
        if (descriptor.localData == nullptr) {
            descriptor.state.store(CpuState::Failed, MemoryOrder::Release);
            continue;
        }
        descriptor.state.store(CpuState::Starting, MemoryOrder::Release);
        descriptor.startupStage.store(1, MemoryOrder::Release);
        *trampolinePointer(&apStartupCr3) = getKernelAddressSpaceRoot();
        *trampolinePointer(&apStartupStack) =
            reinterpret_cast<uint64_t>(descriptor.localData->kernelStack);
        *trampolinePointer(&apStartupCpuLocal) =
            reinterpret_cast<uint64_t>(descriptor.localData);
        *trampolinePointer(&apStartupEntry) =
            reinterpret_cast<uint64_t>(applicationProcessorEntry);

        sendInitInterProcessorInterrupt(descriptor.apicId);
        startupDelay(1000000);
        sendStartupInterProcessorInterrupt(descriptor.apicId, trampolineVector);
        startupDelay(200000);
        sendStartupInterProcessorInterrupt(descriptor.apicId, trampolineVector);

        uint32_t timeout = 100000000;
        while (descriptor.state.load(MemoryOrder::Acquire) ==
                   CpuState::Starting &&
               timeout-- != 0) {
            asm volatile("pause");
        }
        if (descriptor.state.load(MemoryOrder::Acquire) != CpuState::Online) {
            descriptor.state.store(CpuState::Failed, MemoryOrder::Release);
            serialWrite("[SMP] AP startup failed cpu=");
            serialWriteDecimal(cpuId);
            serialWrite(" apic=");
            serialWriteDecimal(descriptor.apicId);
            serialWrite(" stage=");
            serialWriteDecimal(
                descriptor.startupStage.load(MemoryOrder::Acquire));
            serialWrite("\n");
        }
    }
    unmapPage(trampolineAddress);
    serialWrite("[SMP] online CPUs=");
    serialWriteDecimal(getOnlineCpuCount());
    serialWrite("\n");
}

uint32_t getDiscoveredCpuCount() { return discoveredCpuCount; }

uint32_t getOnlineCpuCount() {
    return onlineCpuCount.load(MemoryOrder::Acquire);
}

CpuDescriptor* getCpuDescriptor(uint32_t cpuId) {
    return cpuId < discoveredCpuCount ? &cpuDescriptors[cpuId] : nullptr;
}

CpuLocalData* getCpuLocalData(uint32_t cpuId) {
    CpuDescriptor* descriptor = getCpuDescriptor(cpuId);
    return descriptor == nullptr ? nullptr : descriptor->localData;
}

void requestRemoteReschedule(uint32_t cpuId) {
    CpuDescriptor* descriptor = getCpuDescriptor(cpuId);
    if (descriptor != nullptr && cpuId != getCurrentCpuId() &&
        descriptor->state.load(MemoryOrder::Acquire) == CpuState::Online) {
        sendInterProcessorInterrupt(descriptor->apicId, rescheduleIpiVector);
    }
}

void requestTlbShootdown(uint64_t virtualAddress) {
    if (getOnlineCpuCount() <= 1) {
        return;
    }
    tlbShootdownLock.lock();
    const uint64_t flags = disableInterrupts();
    tlbShootdownAddress.store(virtualAddress, MemoryOrder::Release);
    tlbShootdownAcknowledgements.store(0, MemoryOrder::Release);
    uint32_t targets = 0;
    for (uint32_t cpuId = 0; cpuId < discoveredCpuCount; ++cpuId) {
        CpuDescriptor& descriptor = cpuDescriptors[cpuId];
        if (cpuId != getCurrentCpuId() &&
            descriptor.state.load(MemoryOrder::Acquire) == CpuState::Online) {
            ++targets;
            sendInterProcessorInterrupt(descriptor.apicId,
                                        tlbShootdownIpiVector);
        }
    }
    while (tlbShootdownAcknowledgements.load(MemoryOrder::Acquire) < targets) {
        asm volatile("pause");
    }
    restoreInterrupts(flags);
    tlbShootdownLock.unlock();
}

extern "C" [[noreturn]] void applicationProcessorEntry(CpuLocalData* cpu) {
    CpuDescriptor* descriptor =
        cpu == nullptr ? nullptr : getCpuDescriptor(cpu->cpuId);
    if (descriptor == nullptr) {
        haltCpu();
    }
    descriptor->startupStage.store(2, MemoryOrder::Release);
    initApplicationProcessorCpu(cpu);
    descriptor->startupStage.store(3, MemoryOrder::Release);
    loadIdt();
    initSyscall();
    initThreadSubsystem();
    initScheduler();
    descriptor->startupStage.store(4, MemoryOrder::Release);
    descriptor->state.store(CpuState::Online, MemoryOrder::Release);
    onlineCpuCount.fetchAdd(1, MemoryOrder::Release);
    exitThread(0);
    haltCpu();
}

}  // namespace kernel
