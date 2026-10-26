#include "kernel/smp.h"

#include "kernel/acpi.h"
#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/interrupt.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

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
    uint8_t taskorId;
    uint8_t apicId;
    uint32_t flags;
};

struct [[gnu::packed]] MadtLocalX2Apic {
    MadtEntryHeader header;
    uint16_t reserved;
    uint32_t x2ApicId;
    uint32_t flags;
    uint32_t taskorId;
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

bool taskorEnabled(uint32_t flags) { return (flags & 1U) != 0; }

uint32_t countEnabledTaskors(const Madt* madt) {
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
            taskorEnabled(
                reinterpret_cast<const MadtLocalApic*>(cursor)->flags)) {
            ++count;
        } else if (header->type == 9 &&
                   header->length >= sizeof(MadtLocalX2Apic) &&
                   taskorEnabled(
                       reinterpret_cast<const MadtLocalX2Apic*>(cursor)
                           ->flags)) {
            ++count;
        }
        cursor += header->length;
    }
    return count;
}

void initializeDescriptor(kernel::CpuDescriptor& descriptor, uint32_t cpuId,
                          uint32_t taskorId, uint32_t apicId,
                          kernel::CpuState state) {
    descriptor.setCpuId(cpuId);
    descriptor.setTaskorId(taskorId);
    descriptor.setApicId(apicId);
    descriptor.setEnabled(true);
    descriptor.setLocalData(nullptr);
    descriptor.getState().store(state, kernel::MemoryOrder::Relaxed);
    descriptor.getStartupStage().store(0, kernel::MemoryOrder::Relaxed);
}

void rescheduleIpiHandler(kernel::interrupt::Context*) {
    kernel::schedule::requestReschedule();
}

void tlbShootdownIpiHandler(kernel::interrupt::Context*) {
    const uint64_t address =
        tlbShootdownAddress.load(kernel::MemoryOrder::Acquire);
    asm volatile("invlpg (%0)" : : "r"(address) : "memory");
    tlbShootdownAcknowledgements.fetchAdd(1, kernel::MemoryOrder::Release);
}

}  // namespace

namespace kernel::smp {

void initSmp(const BootInfo* bootInfo) {
    const Madt* madt =
        reinterpret_cast<const Madt*>(acpi::findAcpiTable(bootInfo, "APIC"));
    if (madt == nullptr || madt->header.length < sizeof(Madt)) {
        serial::serialWrite("[SMP] ACPI MADT unavailable; using BSP only\n");
        discoveredCpuCount = 1;
    } else {
        discoveredCpuCount = countEnabledTaskors(madt);
        if (discoveredCpuCount == 0) {
            discoveredCpuCount = 1;
        } else if (discoveredCpuCount > maximumSupportedCpus) {
            discoveredCpuCount = maximumSupportedCpus;
        }
    }

    cpuDescriptors = static_cast<CpuDescriptor*>(
        heap::kernelAllocate(sizeof(CpuDescriptor) * discoveredCpuCount));
    if (cpuDescriptors == nullptr) {
        serial::serialWrite("[SMP] unable to allocate CPU descriptors\n");
        cpu::halt();
    }
    clearMemory(cpuDescriptors, sizeof(CpuDescriptor) * discoveredCpuCount);
    initializeDescriptor(cpuDescriptors[0], 0, 0,
                         cpu::getCurrent()->getApicId(), CpuState::Online);
    cpuDescriptors[0].setLocalData(cpu::getCurrent());

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
            uint32_t taskorId = 0;
            uint32_t apicId   = 0xffffffffU;
            uint32_t flags    = 0;
            if (header->type == 0 && header->length >= sizeof(MadtLocalApic)) {
                const MadtLocalApic* entry =
                    reinterpret_cast<const MadtLocalApic*>(cursor);
                taskorId = entry->taskorId;
                apicId   = entry->apicId;
                flags    = entry->flags;
            } else if (header->type == 9 &&
                       header->length >= sizeof(MadtLocalX2Apic)) {
                const MadtLocalX2Apic* entry =
                    reinterpret_cast<const MadtLocalX2Apic*>(cursor);
                taskorId = entry->taskorId;
                apicId   = entry->x2ApicId;
                flags    = entry->flags;
            }
            if (taskorEnabled(flags)) {
                if (apicId == cpu::getCurrent()->getApicId()) {
                    cpuDescriptors[0].setTaskorId(taskorId);
                } else if (apicId <= 0xff) {
                    initializeDescriptor(cpuDescriptors[nextCpuId], nextCpuId,
                                         taskorId, apicId, CpuState::Offline);
                    ++nextCpuId;
                }
            }
            cursor += header->length;
        }
    }
    discoveredCpuCount = nextCpuId == 0 ? 1 : nextCpuId;
    onlineCpuCount.store(1, MemoryOrder::Release);
    interrupt::registerHandler(rescheduleIpiVector, rescheduleIpiHandler);
    interrupt::registerHandler(tlbShootdownIpiVector, tlbShootdownIpiHandler);
    serial::serialWrite("[SMP] discovered CPUs=");
    serial::serialWriteDecimal(discoveredCpuCount);
    serial::serialWrite(" BSP APIC ID=");
    serial::serialWriteDecimal(cpu::getCurrent()->getApicId());
    serial::serialWrite("\n");
}

void startApplicationTaskors() {
    if (discoveredCpuCount <= 1) {
        serial::serialWrite("[SMP] no application taskors to start\n");
        return;
    }
    const uint64_t trampolineSize =
        reinterpret_cast<uint64_t>(&apTrampolineEnd) -
        reinterpret_cast<uint64_t>(&apTrampolineStart);
    if (trampolineSize > 4096 ||
        !memory::mapPage(trampolineAddress, trampolineAddress, pageWritable)) {
        serial::serialWrite("[SMP] unable to map AP trampoline\n");
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
        descriptor.setLocalData(
            CpuLocalData::prepare(cpuId, descriptor.getApicId()));
        if (descriptor.getLocalData() == nullptr) {
            descriptor.getState().store(CpuState::Failed, MemoryOrder::Release);
            continue;
        }
        descriptor.getState().store(CpuState::Starting, MemoryOrder::Release);
        descriptor.getStartupStage().store(1, MemoryOrder::Release);
        *trampolinePointer(&apStartupCr3) = memory::getKernelAddressSpaceRoot();
        *trampolinePointer(&apStartupStack) = reinterpret_cast<uint64_t>(
            descriptor.getLocalData()->getKernelStack());
        *trampolinePointer(&apStartupCpuLocal) =
            reinterpret_cast<uint64_t>(descriptor.getLocalData());
        *trampolinePointer(&apStartupEntry) =
            reinterpret_cast<uint64_t>(smp::applicationTaskorEntry);

        localapic::sendInitInterTaskorInterrupt(descriptor.getApicId());
        startupDelay(1000000);
        localapic::sendStartupInterTaskorInterrupt(descriptor.getApicId(),
                                                   trampolineVector);
        startupDelay(200000);
        localapic::sendStartupInterTaskorInterrupt(descriptor.getApicId(),
                                                   trampolineVector);

        uint32_t timeout = 100000000;
        while (descriptor.getState().load(MemoryOrder::Acquire) ==
                   CpuState::Starting &&
               timeout-- != 0) {
            asm volatile("pause");
        }
        if (descriptor.getState().load(MemoryOrder::Acquire) !=
            CpuState::Online) {
            descriptor.getState().store(CpuState::Failed, MemoryOrder::Release);
            serial::serialWrite("[SMP] AP startup failed cpu=");
            serial::serialWriteDecimal(cpuId);
            serial::serialWrite(" apic=");
            serial::serialWriteDecimal(descriptor.getApicId());
            serial::serialWrite(" stage=");
            serial::serialWriteDecimal(
                descriptor.getStartupStage().load(MemoryOrder::Acquire));
            serial::serialWrite("\n");
        }
    }
    memory::unmapPage(trampolineAddress);
    serial::serialWrite("[SMP] online CPUs=");
    serial::serialWriteDecimal(smp::getOnlineCpuCount());
    serial::serialWrite("\n");
}

uint32_t getDiscoveredCpuCount() { return discoveredCpuCount; }

uint32_t getOnlineCpuCount() {
    return onlineCpuCount.load(MemoryOrder::Acquire);
}

CpuDescriptor* getCpuDescriptor(uint32_t cpuId) {
    return cpuId < discoveredCpuCount ? &cpuDescriptors[cpuId] : nullptr;
}

CpuLocalData* getCpuLocalData(uint32_t cpuId) {
    CpuDescriptor* descriptor = smp::getCpuDescriptor(cpuId);
    return descriptor == nullptr ? nullptr : descriptor->getLocalData();
}

void requestRemoteReschedule(uint32_t cpuId) {
    CpuDescriptor* descriptor = smp::getCpuDescriptor(cpuId);
    if (descriptor != nullptr && cpuId != cpu::getCurrentId() &&
        descriptor->getState().load(MemoryOrder::Acquire) == CpuState::Online) {
        localapic::sendInterTaskorInterrupt(descriptor->getApicId(),
                                            rescheduleIpiVector);
    }
}

void requestTlbShootdown(uint64_t virtualAddress) {
    if (smp::getOnlineCpuCount() <= 1) {
        return;
    }
    // A killed/preempted caller must not strand the global shootdown lock.
    // Do not mask interrupts before acquiring: contenders must service IPIs.
    cpu::disablePreemption();
    tlbShootdownLock.lock();
    const uint64_t flags = interrupt::disable();
    tlbShootdownAddress.store(virtualAddress, MemoryOrder::Release);
    tlbShootdownAcknowledgements.store(0, MemoryOrder::Release);
    uint32_t targets = 0;
    for (uint32_t cpuId = 0; cpuId < discoveredCpuCount; ++cpuId) {
        CpuDescriptor& descriptor = cpuDescriptors[cpuId];
        if (cpuId != cpu::getCurrentId() &&
            descriptor.getState().load(MemoryOrder::Acquire) ==
                CpuState::Online) {
            ++targets;
            localapic::sendInterTaskorInterrupt(descriptor.getApicId(),
                                                tlbShootdownIpiVector);
        }
    }
    while (tlbShootdownAcknowledgements.load(MemoryOrder::Acquire) < targets) {
        asm volatile("pause");
    }
    tlbShootdownLock.unlock();
    interrupt::restore(flags);
    cpu::enablePreemption();
}

extern "C" [[noreturn]] void applicationTaskorEntry(CpuLocalData* cpu) {
    CpuDescriptor* descriptor =
        cpu == nullptr ? nullptr : smp::getCpuDescriptor(cpu->getCpuId());
    if (descriptor == nullptr) {
        cpu::halt();
    }
    descriptor->getStartupStage().store(2, MemoryOrder::Release);
    cpu->initApplicationTaskorCpu();
    descriptor->getStartupStage().store(3, MemoryOrder::Release);
    interrupt::loadIdt();
    syscall::init();
    Thread::init();
    schedule::init();
    descriptor->getStartupStage().store(4, MemoryOrder::Release);
    descriptor->getState().store(CpuState::Online, MemoryOrder::Release);
    onlineCpuCount.fetchAdd(1, MemoryOrder::Release);
    Thread::exitCurrent(0);
    cpu::halt();
}

}  // namespace kernel::smp
