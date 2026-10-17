#include "kernel/acpi.h"
#include "kernel/boot.h"
#include "kernel/interrupt.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"

namespace {

constexpr uint64_t directMapBase  = 0xffff800000000000ULL;
constexpr uint64_t directMapSize  = 64ULL * 1024 * 1024 * 1024;
constexpr uint32_t maxIoApics     = 8;
constexpr uint32_t legacyIrqCount = 16;

struct [[gnu::packed]] Madt {
    kernel::AcpiSdtHeader header;
    uint32_t localApicAddress;
    uint32_t flags;
};

struct [[gnu::packed]] MadtEntryHeader {
    uint8_t type;
    uint8_t length;
};

struct [[gnu::packed]] MadtIoApic {
    MadtEntryHeader header;
    uint8_t id;
    uint8_t reserved;
    uint32_t address;
    uint32_t globalSystemInterruptBase;
};

struct [[gnu::packed]] MadtInterruptOverride {
    MadtEntryHeader header;
    uint8_t bus;
    uint8_t source;
    uint32_t globalSystemInterrupt;
    uint16_t flags;
};

struct IoApic {
    volatile uint32_t* registers;
    uint32_t globalSystemInterruptBase;
    uint32_t redirectionCount;
};

struct IrqRoute {
    uint32_t globalSystemInterrupt;
    uint16_t flags;
};

IoApic ioApics[maxIoApics];
uint32_t ioApicCount;
IrqRoute irqRoutes[legacyIrqCount];
kernel::SpinLock ioApicLock;

template <typename Type>
Type* mapPhysical(uint64_t physicalAddress) {
    if (physicalAddress >= directMapSize) {
        return nullptr;
    }
    return reinterpret_cast<Type*>(directMapBase + physicalAddress);
}

const Madt* findMadt(const kernel::BootInfo* bootInfo) {
    return reinterpret_cast<const Madt*>(
        kernel::acpi::findAcpiTable(bootInfo, "APIC"));
}

uint32_t readIoApic(const IoApic& ioApic, uint8_t index) {
    ioApic.registers[0] = index;
    return ioApic.registers[4];
}

void writeIoApic(const IoApic& ioApic, uint8_t index, uint32_t value) {
    ioApic.registers[0] = index;
    ioApic.registers[4] = value;
}

IoApic* findIoApic(uint32_t globalSystemInterrupt) {
    for (uint32_t index = 0; index < ioApicCount; ++index) {
        IoApic& ioApic = ioApics[index];
        if (globalSystemInterrupt >= ioApic.globalSystemInterruptBase &&
            globalSystemInterrupt <
                ioApic.globalSystemInterruptBase + ioApic.redirectionCount) {
            return &ioApic;
        }
    }
    return nullptr;
}

void maskGlobalInterruptLocked(uint32_t globalSystemInterrupt) {
    IoApic* ioApic = findIoApic(globalSystemInterrupt);
    if (ioApic == nullptr) {
        return;
    }
    const uint32_t pin =
        globalSystemInterrupt - ioApic->globalSystemInterruptBase;
    const uint8_t lowRegister = static_cast<uint8_t>(0x10 + pin * 2);
    writeIoApic(*ioApic, lowRegister,
                readIoApic(*ioApic, lowRegister) | (1U << 16));
}

}  // namespace

namespace kernel::interrupt {

void initControllers(const BootInfo* bootInfo) {
    for (uint32_t irq = 0; irq < legacyIrqCount; ++irq) {
        irqRoutes[irq].globalSystemInterrupt = irq;
        irqRoutes[irq].flags                 = 0;
    }

    const Madt* madt = findMadt(bootInfo);
    if (madt == nullptr || madt->header.length < sizeof(Madt)) {
        serial::serialWrite(
            "[INTERRUPT] ACPI MADT unavailable; external IRQ routing "
            "disabled\n");
        return;
    }

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
        if (header->type == 1 && header->length >= sizeof(MadtIoApic) &&
            ioApicCount < maxIoApics) {
            const MadtIoApic* entry =
                reinterpret_cast<const MadtIoApic*>(cursor);
            IoApic& ioApic   = ioApics[ioApicCount];
            ioApic.registers = mapPhysical<volatile uint32_t>(entry->address);
            if (ioApic.registers == nullptr) {
                cursor += header->length;
                continue;
            }
            ioApic.globalSystemInterruptBase = entry->globalSystemInterruptBase;
            ioApic.redirectionCount =
                ((readIoApic(ioApic, 1) >> 16) & 0xff) + 1;
            ++ioApicCount;
        } else if (header->type == 2 &&
                   header->length >= sizeof(MadtInterruptOverride)) {
            const MadtInterruptOverride* entry =
                reinterpret_cast<const MadtInterruptOverride*>(cursor);
            if (entry->bus == 0 && entry->source < legacyIrqCount) {
                irqRoutes[entry->source].globalSystemInterrupt =
                    entry->globalSystemInterrupt;
                irqRoutes[entry->source].flags = entry->flags;
            }
        }
        cursor += header->length;
    }

    {
        InterruptSpinLockGuard guard(ioApicLock);
        for (uint32_t index = 0; index < ioApicCount; ++index) {
            const IoApic& ioApic = ioApics[index];
            for (uint32_t pin = 0; pin < ioApic.redirectionCount; ++pin) {
                maskGlobalInterruptLocked(ioApic.globalSystemInterruptBase +
                                          pin);
            }
        }
    }

    if (ioApicCount == 0) {
        serial::serialWrite(
            "[INTERRUPT] no I/O APIC found; external IRQ routing disabled\n");
        return;
    }
    serial::serialWrite("[INTERRUPT] I/O APIC routing initialized\n");
}

bool setIrq(uint8_t irq, uint8_t vector, uint32_t apicId, bool levelTriggered) {
    if (irq >= legacyIrqCount || vector < 32) {
        return false;
    }
    InterruptSpinLockGuard guard(ioApicLock);
    const IrqRoute& route = irqRoutes[irq];
    IoApic* ioApic        = findIoApic(route.globalSystemInterrupt);
    if (ioApic == nullptr) {
        return false;
    }
    const uint32_t pin =
        route.globalSystemInterrupt - ioApic->globalSystemInterruptBase;
    const uint8_t lowRegister = static_cast<uint8_t>(0x10 + pin * 2);
    uint32_t low              = vector;
    const uint16_t polarity   = route.flags & 0x3;
    const uint16_t trigger    = (route.flags >> 2) & 0x3;
    if (polarity == 3 || levelTriggered) {
        low |= 1U << 13;
    }
    if (trigger == 3 || levelTriggered) {
        low |= 1U << 15;
    }
    writeIoApic(*ioApic, static_cast<uint8_t>(lowRegister + 1), apicId << 24);
    writeIoApic(*ioApic, lowRegister, low);
    return true;
}

void maskIrq(uint8_t irq) {
    if (irq >= legacyIrqCount) {
        return;
    }
    InterruptSpinLockGuard guard(ioApicLock);
    maskGlobalInterruptLocked(irqRoutes[irq].globalSystemInterrupt);
}

}  // namespace kernel::interrupt
