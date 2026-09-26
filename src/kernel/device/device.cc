#include "kernel/device.h"

#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/heap.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/new.h"
#include "kernel/page.h"
#include "kernel/process.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"

namespace {

constexpr uint64_t pageSize     = 4096;
constexpr uint8_t irqVectorBase = 64;
constexpr uint8_t maximumIrq    = 15;
kernel::SpinLock pciLock;
kernel::SpinLock bindingLock;
kernel::InterruptBinding* bindings[16];
kernel::FramebufferInfo bootFramebuffer{};

uint64_t alignDown(uint64_t value) { return value & ~(pageSize - 1); }

uint64_t alignUp(uint64_t value) {
    return (value + pageSize - 1) & ~(pageSize - 1);
}

bool isPs2Port(uint16_t port) { return port == 0x60 || port == 0x64; }

uint32_t pciAddress(const os::PciFunctionInfo& function, uint16_t offset) {
    return 0x80000000U | (static_cast<uint32_t>(function.bus) << 16) |
           (static_cast<uint32_t>(function.device) << 11) |
           (static_cast<uint32_t>(function.function) << 8) | (offset & 0xfc);
}

uint32_t readConfigRaw(const os::PciFunctionInfo& function, uint16_t offset) {
    const uint32_t address = pciAddress(function, offset);
    asm volatile("outl %0, %1" : : "a"(address), "Nd"(0xcf8));
    uint32_t value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(0xcfc));
    return value;
}

void writeConfigRaw(const os::PciFunctionInfo& function, uint16_t offset,
                    uint32_t value) {
    const uint32_t address = pciAddress(function, offset);
    asm volatile("outl %0, %1" : : "a"(address), "Nd"(0xcf8));
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(0xcfc));
}

bool validFunction(const os::PciFunctionInfo& function) {
    return function.segment == 0 && function.device < 32 &&
           function.function < 8;
}

void irqHandler(kernel::InterruptContext* context) {
    const uint64_t vector = context->vector;
    if (vector < irqVectorBase || vector > irqVectorBase + maximumIrq) {
        return;
    }
    kernel::InterruptBinding* binding = bindings[vector - irqVectorBase];
    if (binding != nullptr) {
        kernel::notifyEndpointFromInterrupt(binding->endpoint(), 0x495251,
                                            binding->irq());
    }
}

}  // namespace

namespace kernel {

void initDeviceSubsystem(const BootInfo* bootInfo) {
    bootFramebuffer = bootInfo == nullptr ? FramebufferInfo{}
                                          : bootInfo->framebuffer;
    serialWrite("[DISPLAY] boot framebuffer address=");
    serialWriteHex(bootFramebuffer.address);
    serialWrite(" width=");
    serialWriteDecimal(bootFramebuffer.width);
    serialWrite(" height=");
    serialWriteDecimal(bootFramebuffer.height);
    serialWrite(" pitch=");
    serialWriteDecimal(bootFramebuffer.pitch);
    serialWrite(" bpp=");
    serialWriteDecimal(bootFramebuffer.bitsPerPixel);
    serialWrite(" type=");
    serialWriteDecimal(bootFramebuffer.type);
    serialWrite("\n");
}

bool PortAccess::read8(const Process& process, uint16_t port, uint8_t& value) {
    if (!process.hasCapability(processCapabilityDevice) || !isPs2Port(port)) {
        return false;
    }
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return true;
}

bool PortAccess::write8(const Process& process, uint16_t port, uint8_t value) {
    if (!process.hasCapability(processCapabilityDevice) || !isPs2Port(port)) {
        return false;
    }
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    return true;
}

bool DisplayAccess::mapFramebuffer(Process& process, uint64_t userAddress,
                                   os::DisplayInfo& info) {
    if (!process.hasCapability(processCapabilityDisplay) ||
        bootFramebuffer.address == 0 || bootFramebuffer.width == 0 ||
        bootFramebuffer.height == 0 || bootFramebuffer.pitch == 0 ||
        bootFramebuffer.bitsPerPixel < 24 ||
        (userAddress & (pageSize - 1)) != 0 || userAddress < 0x10000) {
        serialWrite("[DISPLAY] framebuffer mapping validation failed\n");
        return false;
    }
    const uint64_t physicalBase = alignDown(bootFramebuffer.address);
    const uint64_t offset = bootFramebuffer.address - physicalBase;
    const uint64_t visibleSize =
        static_cast<uint64_t>(bootFramebuffer.pitch) * bootFramebuffer.height;
    if (visibleSize == 0 || visibleSize > 64ULL * 1024 * 1024 ||
        visibleSize > ~0ULL - offset) {
        return false;
    }
    const uint64_t mappedSize = alignUp(offset + visibleSize);
    if (userAddress + mappedSize < userAddress ||
        userAddress + mappedSize >= 0x0000800000000000ULL) {
        return false;
    }
    PreemptionSpinLockGuard addressGuard(process.addressSpace().lock());
    uint64_t mapped = 0;
    for (; mapped < mappedSize; mapped += pageSize) {
        if (!mapPageInAddressSpace(
                process.addressSpace().rootPhysicalAddress(),
                userAddress + mapped, physicalBase + mapped,
                pageUser | pageWritable | pageNoExecute | pageCacheDisable |
                    pageExternal)) {
            serialWrite("[DISPLAY] framebuffer page mapping failed at ");
            serialWriteHex(mapped);
            serialWrite("\n");
            while (mapped != 0) {
                mapped -= pageSize;
                unmapPageInAddressSpace(
                    process.addressSpace().rootPhysicalAddress(),
                    userAddress + mapped);
            }
            return false;
        }
    }
    info.address = userAddress + offset;
    info.size = mappedSize;
    info.width = bootFramebuffer.width;
    info.height = bootFramebuffer.height;
    info.pitch = bootFramebuffer.pitch;
    info.bitsPerPixel = bootFramebuffer.bitsPerPixel;
    info.redPosition = bootFramebuffer.redPosition;
    info.greenPosition = bootFramebuffer.greenPosition;
    info.bluePosition = bootFramebuffer.bluePosition;
    if (bootFramebuffer.bitsPerPixel == 32 &&
        bootFramebuffer.redPosition == 16 &&
        bootFramebuffer.greenPosition == 8 &&
        bootFramebuffer.bluePosition == 0) {
        info.format = os::PixelFormat::Bgra;
    } else if (bootFramebuffer.bitsPerPixel == 32 &&
               bootFramebuffer.redPosition == 0 &&
               bootFramebuffer.greenPosition == 8 &&
               bootFramebuffer.bluePosition == 16) {
        info.format = os::PixelFormat::Rgba;
    } else {
        info.format = os::PixelFormat::Unknown;
    }
    return true;
}

uint32_t PciAccess::enumerate(os::PciFunctionInfo* functions,
                              uint32_t capacity) {
    if (functions == nullptr || capacity == 0) {
        return 0;
    }
    uint32_t count = 0;
    for (uint16_t bus = 0; bus < 256 && count < capacity; ++bus) {
        for (uint8_t device = 0; device < 32 && count < capacity; ++device) {
            os::PciFunctionInfo probe{};
            probe.bus      = static_cast<uint8_t>(bus);
            probe.device   = device;
            probe.function = 0;
            uint32_t identity;
            if (!read(probe, 0, identity) || (identity & 0xffff) == 0xffff) {
                continue;
            }
            uint32_t header;
            read(probe, 0x0c, header);
            const uint8_t functionCount = (header & 0x00800000U) != 0 ? 8 : 1;
            for (uint8_t function = 0;
                 function < functionCount && count < capacity; ++function) {
                probe.function = function;
                if (!read(probe, 0, identity) ||
                    (identity & 0xffff) == 0xffff) {
                    continue;
                }
                uint32_t classValue;
                read(probe, 8, classValue);
                read(probe, 0x0c, header);
                os::PciFunctionInfo& result = functions[count++];
                result                      = probe;
                result.vendorId             = identity & 0xffff;
                result.deviceId             = identity >> 16;
                result.classCode            = classValue >> 24;
                result.subclass             = classValue >> 16;
                result.programmingInterface = classValue >> 8;
                result.headerType           = header >> 16;
            }
        }
    }
    return count;
}

bool PciAccess::read(const os::PciFunctionInfo& function, uint16_t offset,
                     uint32_t& value) {
    if (!validFunction(function) || offset >= 256 || (offset & 3) != 0) {
        return false;
    }
    InterruptSpinLockGuard guard(pciLock);
    value = readConfigRaw(function, offset);
    return true;
}

bool PciAccess::write(const os::PciFunctionInfo& function, uint16_t offset,
                      uint32_t value) {
    if (!validFunction(function) || offset >= 256 || (offset & 3) != 0) {
        return false;
    }
    InterruptSpinLockGuard guard(pciLock);
    writeConfigRaw(function, offset, value);
    return true;
}

bool PciAccess::getMemoryBar(const os::PciFunctionInfo& function,
                             uint8_t barIndex, uint64_t& base, uint64_t& size) {
    if (!validFunction(function) || barIndex >= 6) {
        return false;
    }
    InterruptSpinLockGuard guard(pciLock);
    const uint16_t offset      = static_cast<uint16_t>(0x10 + barIndex * 4);
    const uint32_t originalLow = readConfigRaw(function, offset);
    if ((originalLow & 1) != 0) {
        return false;
    }
    const bool is64Bit = ((originalLow >> 1) & 3) == 2;
    if (is64Bit && barIndex == 5) {
        return false;
    }
    const uint32_t originalHigh =
        is64Bit ? readConfigRaw(function, offset + 4) : 0;
    const uint32_t command = readConfigRaw(function, 4);
    writeConfigRaw(function, 4, command & ~3U);
    writeConfigRaw(function, offset, 0xffffffffU);
    if (is64Bit) {
        writeConfigRaw(function, offset + 4, 0xffffffffU);
    }
    const uint32_t maskLow  = readConfigRaw(function, offset);
    const uint32_t maskHigh = is64Bit ? readConfigRaw(function, offset + 4) : 0;
    writeConfigRaw(function, offset, originalLow);
    if (is64Bit) {
        writeConfigRaw(function, offset + 4, originalHigh);
    }
    writeConfigRaw(function, 4, command);

    base =
        (static_cast<uint64_t>(originalHigh) << 32) | (originalLow & ~0xfULL);
    const uint64_t mask =
        (static_cast<uint64_t>(maskHigh) << 32) | (maskLow & ~0xfULL);
    size = is64Bit ? (~mask) + 1
                   : static_cast<uint64_t>(
                         static_cast<uint32_t>(~(maskLow & ~0xfU)) + 1U);
    return base != 0 && size != 0;
}

MmioRegion* MmioRegion::map(Process& process,
                            const os::PciFunctionInfo& function,
                            uint8_t barIndex, uint64_t userAddress,
                            uint64_t size) {
    uint64_t barBase;
    uint64_t barSize;
    if ((userAddress & (pageSize - 1)) != 0 || userAddress < 0x10000 ||
        size == 0 || userAddress + size < userAddress ||
        userAddress + size >= 0x0000800000000000ULL ||
        (size & (pageSize - 1)) != 0 ||
        !PciAccess::getMemoryBar(function, barIndex, barBase, barSize) ||
        size > barSize || (barBase & (pageSize - 1)) != 0) {
        return nullptr;
    }
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        if (!mapPageInAddressSpace(
                process.addressSpace().rootPhysicalAddress(),
                userAddress + offset, barBase + offset,
                pageUser | pageWritable | pageNoExecute | pageCacheDisable |
                    pageExternal)) {
            while (offset != 0) {
                offset -= pageSize;
                unmapPageInAddressSpace(
                    process.addressSpace().rootPhysicalAddress(),
                    userAddress + offset);
            }
            return nullptr;
        }
    }
    void* storage = kernelAllocate(sizeof(MmioRegion));
    MmioRegion* region =
        storage == nullptr ? nullptr : new (storage) MmioRegion;
    if (region == nullptr) {
        return nullptr;
    }
    region->physicalAddress_ = barBase;
    region->virtualAddress_  = userAddress;
    region->size_            = size;
    return region;
}

DmaBuffer* DmaBuffer::create(Process& process, uint64_t userAddress,
                             os::DmaBufferInfo& info) {
    if ((userAddress & (pageSize - 1)) != 0 || userAddress < 0x10000 ||
        userAddress + pageSize < userAddress ||
        userAddress + pageSize >= 0x0000800000000000ULL) {
        return nullptr;
    }
    const uint64_t physical = allocatePhysicalPage();
    if (physical == 0 ||
        !mapPageInAddressSpace(process.addressSpace().rootPhysicalAddress(),
                               userAddress, physical,
                               pageUser | pageWritable | pageNoExecute)) {
        if (physical != 0) {
            freePhysicalPage(physical);
        }
        return nullptr;
    }
    void* storage     = kernelAllocate(sizeof(DmaBuffer));
    DmaBuffer* buffer = storage == nullptr ? nullptr : new (storage) DmaBuffer;
    if (buffer == nullptr) {
        return nullptr;
    }
    buffer->physicalAddress_ = physical;
    buffer->virtualAddress_  = userAddress;
    buffer->size_            = pageSize;
    info.virtualAddress      = userAddress;
    info.deviceAddress       = physical;
    info.size                = pageSize;
    return buffer;
}

InterruptBinding* InterruptBinding::create(Process&, uint8_t irq,
                                           Endpoint* endpoint) {
    if (irq > maximumIrq || endpoint == nullptr) {
        return nullptr;
    }
    InterruptSpinLockGuard guard(bindingLock);
    if (bindings[irq] != nullptr) {
        return nullptr;
    }
    void* storage = kernelAllocate(sizeof(InterruptBinding));
    InterruptBinding* binding =
        storage == nullptr ? nullptr : new (storage) InterruptBinding;
    if (binding == nullptr ||
        !registerInterruptHandler(irqVectorBase + irq, irqHandler)) {
        return nullptr;
    }
    if (!configureIrq(irq, irqVectorBase + irq, getCurrentCpu()->apicId)) {
        unregisterInterruptHandler(irqVectorBase + irq);
        return nullptr;
    }
    binding->irq_      = irq;
    binding->endpoint_ = endpoint;
    bindings[irq]      = binding;
    return binding;
}

uint8_t InterruptBinding::irq() const { return irq_; }

Endpoint* InterruptBinding::endpoint() const { return endpoint_; }

}  // namespace kernel
