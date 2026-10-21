#include "kernel/device.h"

#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/new.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"

namespace {

constexpr uint64_t pageSize     = 4096;
constexpr uint8_t irqVectorBase = 64;
constexpr uint8_t maximumIrq    = 15;
kernel::SpinLock bindingLock;
kernel::device::InterruptBinding* bindings[16];
kernel::FramebufferInfo bootFramebuffer{};

uint64_t alignDown(uint64_t value) { return value & ~(pageSize - 1); }

uint64_t alignUp(uint64_t value) {
    return (value + pageSize - 1) & ~(pageSize - 1);
}

void irqHandler(kernel::interrupt::Context* context) {
    const uint64_t vector = context->vector;
    if (vector < irqVectorBase || vector > irqVectorBase + maximumIrq) {
        return;
    }
    kernel::InterruptSpinLockGuard guard(bindingLock);
    kernel::device::InterruptBinding* binding =
        bindings[vector - irqVectorBase];
    if (binding != nullptr) {
        if (binding->requiresAcknowledgement())
            kernel::interrupt::maskIrq(binding->irq());
        (binding->endpoint())->notifyFromInterrupt(0x495251, binding->irq());
    }
}

}  // namespace

namespace kernel::device {

void init(const BootInfo* bootInfo) {
    bootFramebuffer =
        bootInfo == nullptr ? FramebufferInfo{} : bootInfo->framebuffer;
    serial::serialWrite("[DISPLAY] boot framebuffer address=");
    serial::serialWriteHex(bootFramebuffer.address);
    serial::serialWrite(" width=");
    serial::serialWriteDecimal(bootFramebuffer.width);
    serial::serialWrite(" height=");
    serial::serialWriteDecimal(bootFramebuffer.height);
    serial::serialWrite(" pitch=");
    serial::serialWriteDecimal(bootFramebuffer.pitch);
    serial::serialWrite(" bpp=");
    serial::serialWriteDecimal(bootFramebuffer.bitsPerPixel);
    serial::serialWrite(" type=");
    serial::serialWriteDecimal(bootFramebuffer.type);
    serial::serialWrite("\n");
}

bool DisplayAccess::mapFramebuffer(Task& task, uint64_t userAddress,
                                   os::DisplayInfo& info) {
    if (!task.hasCapability(taskCapabilityDisplay) ||
        bootFramebuffer.address == 0 || bootFramebuffer.width == 0 ||
        bootFramebuffer.height == 0 || bootFramebuffer.pitch == 0 ||
        bootFramebuffer.bitsPerPixel < 24 ||
        (userAddress & (pageSize - 1)) != 0 || userAddress < 0x10000) {
        serial::serialWrite(
            "[DISPLAY] framebuffer mapping validation failed\n");
        return false;
    }
    const uint64_t physicalBase = alignDown(bootFramebuffer.address);
    const uint64_t offset       = bootFramebuffer.address - physicalBase;
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
    PreemptionSpinLockGuard addressGuard(task.addressSpace().lock());
    uint64_t mapped = 0;
    for (; mapped < mappedSize; mapped += pageSize) {
        if (!memory::mapPageInAddressSpace(
                task.addressSpace().rootPhysicalAddress(), userAddress + mapped,
                physicalBase + mapped,
                pageUser | pageWritable | pageNoExecute | pageCacheDisable |
                    pageExternal)) {
            serial::serialWrite(
                "[DISPLAY] framebuffer page mapping failed at ");
            serial::serialWriteHex(mapped);
            serial::serialWrite("\n");
            while (mapped != 0) {
                mapped -= pageSize;
                memory::unmapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    userAddress + mapped);
            }
            return false;
        }
    }
    info.address       = userAddress + offset;
    info.size          = mappedSize;
    info.width         = bootFramebuffer.width;
    info.height        = bootFramebuffer.height;
    info.pitch         = bootFramebuffer.pitch;
    info.bitsPerPixel  = bootFramebuffer.bitsPerPixel;
    info.redPosition   = bootFramebuffer.redPosition;
    info.greenPosition = bootFramebuffer.greenPosition;
    info.bluePosition  = bootFramebuffer.bluePosition;
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

MmioRegion* MmioRegion::map(Task& task, const os::PciFunctionInfo& function,
                            uint8_t barIndex, uint64_t userAddress,
                            uint64_t size) {
    if (barIndex >= 6 || (userAddress & (pageSize - 1)) != 0 || userAddress < 0x10000 ||
        size == 0 || userAddress + size < userAddress ||
        userAddress + size >= 0x0000800000000000ULL ||
        (size & (pageSize - 1)) != 0) {
        return nullptr;
    }
    ResourceDomain* domain = ResourceDomain::find(task, function);
    if (!domain) return nullptr;
    auto create = [&]() -> MmioRegion* {
    const uint64_t barBase = domain->barBases_[barIndex], barSize = domain->barSizes_[barIndex];
    if (!barBase || size > barSize || (barBase & (pageSize - 1))) return nullptr;
    PreemptionSpinLockGuard guard(task.addressSpace().lock());
    if (!task.addressSpace().rootPhysicalAddress()) return nullptr;
    for (uint64_t offset = 0; offset < size; offset += pageSize) {
        if (!memory::mapPageInAddressSpace(
                task.addressSpace().rootPhysicalAddress(), userAddress + offset,
                barBase + offset,
                pageUser | pageWritable | pageNoExecute | pageCacheDisable |
                    pageExternal)) {
            while (offset != 0) {
                offset -= pageSize;
                memory::unmapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    userAddress + offset);
            }
            return nullptr;
        }
    }
    void* storage = heap::kernelAllocate(sizeof(MmioRegion));
    MmioRegion* region =
        storage == nullptr ? nullptr : new (storage) MmioRegion;
    if (region == nullptr) {
        for (uint64_t offset = 0; offset < size; offset += pageSize)
            memory::unmapPageInAddressSpace(task.addressSpace().rootPhysicalAddress(), userAddress + offset);
        return nullptr;
    }
    region->physicalAddress_ = barBase;
    region->virtualAddress_  = userAddress;
    region->size_            = size;
    region->mapped_          = true;
    if (!domain->attach(*region)) {
        for (uint64_t offset = 0; offset < size; offset += pageSize)
            memory::unmapPageInAddressSpace(task.addressSpace().rootPhysicalAddress(), userAddress + offset);
        heap::kernelFree(region); return nullptr;
    }
    return region;
    };
    MmioRegion* region = create();
    domain->drop();
    return region;
}

DmaBuffer* DmaBuffer::create(Task& task, ResourceDomain& domain, uint64_t userAddress,
                             os::DmaBufferInfo& info) {
    if (!domain.active(task) || (userAddress & (pageSize - 1)) != 0 || userAddress < 0x10000 ||
        userAddress + pageSize < userAddress ||
        userAddress + pageSize >= 0x0000800000000000ULL) {
        return nullptr;
    }
    PreemptionSpinLockGuard guard(task.addressSpace().lock());
    if (!task.addressSpace().rootPhysicalAddress()) return nullptr;
    const uint64_t physical = memory::allocatePhysicalPage();
    if (physical == 0 ||
        !memory::mapPageInAddressSpace(
            task.addressSpace().rootPhysicalAddress(), userAddress, physical,
            pageUser | pageWritable | pageNoExecute | pageExternal)) {
        if (physical != 0) {
            memory::freePhysicalPage(physical);
        }
        return nullptr;
    }
    void* storage     = heap::kernelAllocate(sizeof(DmaBuffer));
    DmaBuffer* buffer = storage == nullptr ? nullptr : new (storage) DmaBuffer;
    if (buffer == nullptr) {
        memory::unmapPageInAddressSpace(task.addressSpace().rootPhysicalAddress(), userAddress);
        memory::freePhysicalPage(physical);
        return nullptr;
    }
    buffer->physicalAddress_ = physical;
    buffer->virtualAddress_  = userAddress;
    buffer->size_            = pageSize;
    buffer->mapped_          = true;
    memory::KernelMapping::clear(physical, pageSize);
    if (!domain.attach(*buffer)) {
        memory::unmapPageInAddressSpace(task.addressSpace().rootPhysicalAddress(), userAddress);
        memory::freePhysicalPage(physical); heap::kernelFree(buffer); return nullptr;
    }
    info.virtualAddress      = userAddress;
    info.deviceAddress       = physical;
    info.size                = pageSize;
    return buffer;
}

InterruptBinding* InterruptBinding::create(Task& task, uint8_t irq,
                                           Endpoint* endpoint, bool levelTriggered) {
    if (irq > maximumIrq || endpoint == nullptr) {
        return nullptr;
    }
    InterruptSpinLockGuard guard(bindingLock);
    if (bindings[irq] != nullptr) {
        return nullptr;
    }
    void* storage = heap::kernelAllocate(sizeof(InterruptBinding));
    InterruptBinding* binding =
        storage == nullptr ? nullptr : new (storage) InterruptBinding;
    if (binding == nullptr ||
        !interrupt::registerHandler(irqVectorBase + irq, irqHandler)) {
        heap::kernelFree(binding);
        return nullptr;
    }
    const uint32_t apicId = cpu::getCurrent()->getApicId();
    if (!interrupt::setIrq(irq, irqVectorBase + irq, apicId, levelTriggered)) {
        interrupt::unregisterHandler(irqVectorBase + irq);
        heap::kernelFree(binding);
        return nullptr;
    }
    binding->irq_      = irq;
    binding->endpoint_ = endpoint;
    endpoint->retain();
    binding->owner_ = &task;
    binding->apicId_ = apicId;
    binding->levelTriggered_ = levelTriggered;
    bindings[irq]      = binding;
    return binding;
}

uint8_t InterruptBinding::irq() const { return irq_; }

Endpoint* InterruptBinding::endpoint() const { return endpoint_; }

bool InterruptBinding::acknowledge(Task& task) const {
    return owner_ == &task && (!levelTriggered_ ||
        interrupt::setIrq(irq_, irqVectorBase + irq_, apicId_, true));
}

void InterruptBinding::destroy() {
    {
        InterruptSpinLockGuard guard(bindingLock);
        if (bindings[irq_] == this) {
            interrupt::maskIrq(irq_);
            bindings[irq_] = nullptr;
            interrupt::unregisterHandler(irqVectorBase + irq_);
        }
    }
    endpoint_->release();
    heap::kernelFree(this);
}

}  // namespace kernel::device
