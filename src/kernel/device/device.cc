#include "service/common/records.h"
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
                                   os::wire::DisplayInfo& info,uint64_t* generation) {
    if(!userAddress) userAddress=reserveAddress(64*1024*1024ULL);
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
    if(generation)*generation=task.addressSpace().generation();
    return describe(userAddress,info);
}

bool DisplayAccess::describe(uint64_t userAddress,os::wire::DisplayInfo& info) {
    if(!bootFramebuffer.address || !bootFramebuffer.pitch || !bootFramebuffer.height)return false;
    const uint64_t offset=bootFramebuffer.address & 4095;
    const uint64_t visibleSize=static_cast<uint64_t>(bootFramebuffer.pitch)*bootFramebuffer.height;
    const uint64_t mappedSize=alignUp(offset+visibleSize);
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
        info.format = os::wire::PixelFormat::Bgra;
    } else if (bootFramebuffer.bitsPerPixel == 32 &&
               bootFramebuffer.redPosition == 0 &&
               bootFramebuffer.greenPosition == 8 &&
               bootFramebuffer.bluePosition == 16) {
        info.format = os::wire::PixelFormat::Rgba;
    } else {
        info.format = os::wire::PixelFormat::Unknown;
    }
    return true;
}

MmioRegion* MmioRegion::map(Task& task, const os::wire::PciFunctionInfo& function,
                            uint8_t barIndex, uint64_t userAddress,
                            uint64_t size,uint64_t offset) {
    if(!userAddress)userAddress=DisplayAccess::reserveAddress(size);
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
    if (!barBase || offset>barSize || size>barSize-offset || (offset&4095) || (barBase & (pageSize - 1))) return nullptr;
    PreemptionSpinLockGuard guard(task.addressSpace().lock());
    if (!task.addressSpace().rootPhysicalAddress()) return nullptr;
    for (uint64_t pageOffset = 0; pageOffset < size; pageOffset += pageSize) {
        if (!memory::mapPageInAddressSpace(
                task.addressSpace().rootPhysicalAddress(), userAddress + pageOffset,
                barBase + offset + pageOffset,
                pageUser | pageWritable | pageNoExecute | pageCacheDisable |
                    pageExternal)) {
            while (pageOffset != 0) {
                pageOffset -= pageSize;
                memory::unmapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    userAddress + pageOffset);
            }
            return nullptr;
        }
    }
    void* storage = heap::kernelAllocate(sizeof(MmioRegion));
    MmioRegion* region =
        storage == nullptr ? nullptr : new (storage) MmioRegion;
    if (region == nullptr) {
        for (uint64_t pageOffset = 0; pageOffset < size; pageOffset += pageSize)
            memory::unmapPageInAddressSpace(task.addressSpace().rootPhysicalAddress(), userAddress + pageOffset);
        return nullptr;
    }
    region->physicalAddress_ = barBase + offset;
    region->virtualAddress_  = userAddress;
    region->size_            = size;
    region->mapped_          = true;
    if (!domain->attach(*region)) {
        for (uint64_t pageOffset = 0; pageOffset < size; pageOffset += pageSize)
            memory::unmapPageInAddressSpace(task.addressSpace().rootPhysicalAddress(), userAddress + pageOffset);
        heap::kernelFree(region); return nullptr;
    }
    return region;
    };
    MmioRegion* region = create();
    domain->drop();
    return region;
}

DmaBuffer* DmaBuffer::create(Task& task, ResourceDomain& domain, uint64_t userAddress,
                             os::wire::DmaBufferInfo& info,uint64_t size,uint64_t alignment) {
    if(!size || size>8*1024*1024ULL || !alignment || (alignment&(alignment-1)) || alignment>8*1024*1024ULL || !domain.active(task))return nullptr;
    uint32_t order=0;
    while((4096ULL<<order)<size || (4096ULL<<order)<alignment)++order;
    const uint64_t bytes=4096ULL<<order;
    if(!userAddress)userAddress=DisplayAccess::reserveAddress(bytes);
    if((userAddress&4095) || userAddress<0x10000 || userAddress>0x800000000000ULL-bytes)return nullptr;
    PreemptionSpinLockGuard guard(task.addressSpace().lock());
    const uint64_t root=task.addressSpace().rootPhysicalAddress();if(!root)return nullptr;
    const uint64_t physical=memory::allocatePhysicalBlock(order,task.id());if(!physical)return nullptr;
    uint64_t mapped=0;
    for(;mapped<bytes;mapped+=4096)if(!memory::mapPageInAddressSpace(root,userAddress+mapped,physical+mapped,pageUser|pageWritable|pageNoExecute|pageExternal))break;
    if(mapped!=bytes){while(mapped){mapped-=4096;memory::unmapPageInAddressSpace(root,userAddress+mapped);}memory::releasePhysicalBlock(physical,order,task.id());return nullptr;}
    void* storage=heap::kernelAllocate(sizeof(DmaBuffer));auto* buffer=storage?new(storage)DmaBuffer:nullptr;
    if(!buffer){for(uint64_t i=0;i<bytes;i+=4096)memory::unmapPageInAddressSpace(root,userAddress+i);memory::releasePhysicalBlock(physical,order,task.id());return nullptr;}
    buffer->physicalAddress_=physical;buffer->virtualAddress_=userAddress;buffer->size_=bytes;buffer->mapped_=true;
    memory::KernelMapping::clear(physical,bytes);
    if(!domain.attach(*buffer)){for(uint64_t i=0;i<bytes;i+=4096)memory::unmapPageInAddressSpace(root,userAddress+i);memory::releasePhysicalBlock(physical,order,task.id());heap::kernelFree(buffer);return nullptr;}
    info.virtualAddress=userAddress;info.deviceAddress=physical;info.size=bytes;return buffer;
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

namespace kernel::device {
uint64_t DisplayAccess::reserveAddress(uint64_t size) {
    static uint64_t next=0x0000020000000000ULL;
    if(!size || size>UINT64_MAX-8191)return 0;
    const uint64_t bytes=((size+4095)&~4095ULL)+4096;
    uint64_t start=__atomic_load_n(&next,__ATOMIC_RELAXED);
    for(;;){if(start>0x0000700000000000ULL-bytes)return 0;const uint64_t end=start+bytes;
        if(__atomic_compare_exchange_n(&next,&start,end,false,__ATOMIC_RELAXED,__ATOMIC_RELAXED))return start;}
}
uint64_t MmioRegion::address() const {return virtualAddress_;}
uint64_t MmioRegion::size() const {return size_;}
FrameMapping* FrameMapping::create(Task& task,uint64_t address,os::wire::DisplayInfo& info) {
    void* storage=heap::kernelAllocate(sizeof(FrameMapping));if(!storage)return nullptr;
    auto* mapping=new(storage)FrameMapping;
    if(!address)address=DisplayAccess::reserveAddress(64*1024*1024ULL);
    if(!DisplayAccess::mapFramebuffer(task,address,info,&mapping->generation_)){heap::kernelFree(mapping);return nullptr;}
    mapping->owner_=&task;mapping->address_=address;mapping->size_=info.size;task.retain();return mapping;
}
bool FrameMapping::query(Task& task,os::wire::DisplayInfo& info) {
    if(owner_!=&task)return false;
    PreemptionSpinLockGuard guard(task.addressSpace().lock());
    if(task.addressSpace().generation()!=generation_ || !task.addressSpace().rootPhysicalAddress())return false;
    for(uint64_t offset=0;offset<size_;offset+=4096)
        if(memory::getPhysicalAddressInAddressSpace(task.addressSpace().rootPhysicalAddress(),address_+offset)!=(bootFramebuffer.address&~4095ULL)+offset)return false;
    return DisplayAccess::describe(address_,info);
}
void FrameMapping::destroy() {
    Task* owner=owner_;
    {
        PreemptionSpinLockGuard guard(owner->addressSpace().lock());
        if(owner->addressSpace().generation()==generation_ && owner->addressSpace().rootPhysicalAddress())
            for(uint64_t offset=0;offset<size_;offset+=4096)
                if(memory::getPhysicalAddressInAddressSpace(owner->addressSpace().rootPhysicalAddress(),address_+offset)==(bootFramebuffer.address&~4095ULL)+offset)
                    memory::unmapPageInAddressSpace(owner->addressSpace().rootPhysicalAddress(),address_+offset);
    }
    heap::kernelFree(this);owner->release();
}
}
