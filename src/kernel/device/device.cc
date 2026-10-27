#include "kernel/device.h"

#include <new>

#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/sync.h"
#include "kernel/task.h"
#include "service/common/records.h"

namespace kernel::device {

namespace {

constexpr uint64_t pageSize = 4096;
::FramebufferInfo bootFramebuffer{};

uint64_t alignDown(uint64_t value) { return value & ~(pageSize - 1); }

uint64_t alignUp(uint64_t value) {
    return (value + pageSize - 1) & ~(pageSize - 1);
}

}  // namespace

void initDevices(const BootInfo* bootInfo) {
    bootFramebuffer =
        bootInfo == nullptr ? FramebufferInfo{} : bootInfo->getFramebuffer();
    device::serialWrite("[DISPLAY] boot framebuffer address=");
    device::serialWriteHex(bootFramebuffer.getAddress());
    device::serialWrite(" width=");
    device::serialWriteDecimal(bootFramebuffer.getWidth());
    device::serialWrite(" height=");
    device::serialWriteDecimal(bootFramebuffer.getHeight());
    device::serialWrite(" pitch=");
    device::serialWriteDecimal(bootFramebuffer.getPitch());
    device::serialWrite(" bpp=");
    device::serialWriteDecimal(bootFramebuffer.getBitsPerPixel());
    device::serialWrite(" type=");
    device::serialWriteDecimal(bootFramebuffer.getType());
    device::serialWrite("\n");
}

bool mapFramebuffer(::kernel::task::Task& task, uint64_t userAddress,
                    os::wire::DisplayInfo& info, uint64_t* generation) {
    if (!userAddress) userAddress = reserveDisplayAddress(64 * 1024 * 1024ULL);
    if (!task.hasCapability(::kernel::task::taskCapabilityDisplay) ||
        bootFramebuffer.getAddress() == 0 || bootFramebuffer.getWidth() == 0 ||
        bootFramebuffer.getHeight() == 0 || bootFramebuffer.getPitch() == 0 ||
        bootFramebuffer.getBitsPerPixel() < 24 ||
        (userAddress & (pageSize - 1)) != 0 || userAddress < 0x10000) {
        device::serialWrite(
            "[DISPLAY] framebuffer mapping validation failed\n");
        return false;
    }
    const uint64_t physicalBase = alignDown(bootFramebuffer.getAddress());
    const uint64_t offset       = bootFramebuffer.getAddress() - physicalBase;
    const uint64_t visibleSize =
        static_cast<uint64_t>(bootFramebuffer.getPitch()) *
        bootFramebuffer.getHeight();
    if (visibleSize == 0 || visibleSize > 64ULL * 1024 * 1024 ||
        visibleSize > ~0ULL - offset) {
        return false;
    }
    const uint64_t mappedSize = alignUp(offset + visibleSize);
    if (userAddress + mappedSize < userAddress ||
        userAddress + mappedSize >= 0x0000800000000000ULL) {
        return false;
    }
    ::kernel::task::PreemptionSpinLockGuard addressGuard(
        task.addressSpace().lock());
    uint64_t mapped = 0;
    for (; mapped < mappedSize; mapped += pageSize) {
        if (!mm::mapPageInAddressSpace(
                task.addressSpace().rootPhysicalAddress(), userAddress + mapped,
                physicalBase + mapped,
                ::kernel::mm::pageUser | ::kernel::mm::pageWritable |
                    ::kernel::mm::pageNoExecute |
                    ::kernel::mm::pageCacheDisable |
                    ::kernel::mm::pageExternal)) {
            device::serialWrite(
                "[DISPLAY] framebuffer page mapping failed at ");
            device::serialWriteHex(mapped);
            device::serialWrite("\n");
            while (mapped != 0) {
                mapped -= pageSize;
                mm::unmapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    userAddress + mapped);
            }
            return false;
        }
    }
    if (generation) *generation = task.addressSpace().generation();
    return describeDisplay(userAddress, info);
}

bool describeDisplay(uint64_t userAddress, os::wire::DisplayInfo& info) {
    if (!bootFramebuffer.getAddress() || !bootFramebuffer.getPitch() ||
        !bootFramebuffer.getHeight())
        return false;
    const uint64_t offset = bootFramebuffer.getAddress() & 4095;
    const uint64_t visibleSize =
        static_cast<uint64_t>(bootFramebuffer.getPitch()) *
        bootFramebuffer.getHeight();
    const uint64_t mappedSize = alignUp(offset + visibleSize);
    info.address              = userAddress + offset;
    info.size                 = mappedSize;
    info.width                = bootFramebuffer.getWidth();
    info.height               = bootFramebuffer.getHeight();
    info.pitch                = bootFramebuffer.getPitch();
    info.bitsPerPixel         = bootFramebuffer.getBitsPerPixel();
    info.redPosition          = bootFramebuffer.getRedPosition();
    info.greenPosition        = bootFramebuffer.getGreenPosition();
    info.bluePosition         = bootFramebuffer.getBluePosition();
    if (bootFramebuffer.getBitsPerPixel() == 32 &&
        bootFramebuffer.getRedPosition() == 16 &&
        bootFramebuffer.getGreenPosition() == 8 &&
        bootFramebuffer.getBluePosition() == 0) {
        info.format = os::wire::PixelFormat::Bgra;
    } else if (bootFramebuffer.getBitsPerPixel() == 32 &&
               bootFramebuffer.getRedPosition() == 0 &&
               bootFramebuffer.getGreenPosition() == 8 &&
               bootFramebuffer.getBluePosition() == 16) {
        info.format = os::wire::PixelFormat::Rgba;
    } else {
        info.format = os::wire::PixelFormat::Unknown;
    }
    return true;
}

FrameMapping* FrameMapping::create(::kernel::task::Task& task, uint64_t address,
                                   os::wire::DisplayInfo& info) {
    void* storage = mm::allocateHeap(sizeof(FrameMapping));
    if (!storage) return nullptr;
    auto* mapping = new (storage) FrameMapping;
    if (!address) address = reserveDisplayAddress(64 * 1024 * 1024ULL);
    if (!mapFramebuffer(task, address, info, &mapping->generation_)) {
        mm::freeHeap(mapping);
        return nullptr;
    }
    mapping->owner_   = &task;
    mapping->address_ = address;
    mapping->size_    = info.size;
    task.retain();
    return mapping;
}
bool FrameMapping::query(::kernel::task::Task& task,
                         os::wire::DisplayInfo& info) {
    if (owner_ != &task) return false;
    ::kernel::task::PreemptionSpinLockGuard guard(task.addressSpace().lock());
    if (task.addressSpace().generation() != generation_ ||
        !task.addressSpace().rootPhysicalAddress())
        return false;
    for (uint64_t offset = 0; offset < size_; offset += 4096)
        if (mm::getPhysicalAddressInAddressSpace(
                task.addressSpace().rootPhysicalAddress(), address_ + offset) !=
            (bootFramebuffer.getAddress() & ~4095ULL) + offset)
            return false;
    return describeDisplay(address_, info);
}
void FrameMapping::destroy() {
    ::kernel::task::Task* owner = owner_;
    {
        ::kernel::task::PreemptionSpinLockGuard guard(
            owner->addressSpace().lock());
        if (owner->addressSpace().generation() == generation_ &&
            owner->addressSpace().rootPhysicalAddress())
            for (uint64_t offset = 0; offset < size_; offset += 4096)
                if (mm::getPhysicalAddressInAddressSpace(
                        owner->addressSpace().rootPhysicalAddress(),
                        address_ + offset) ==
                    (bootFramebuffer.getAddress() & ~4095ULL) + offset)
                    mm::unmapPageInAddressSpace(
                        owner->addressSpace().rootPhysicalAddress(),
                        address_ + offset);
    }
    mm::freeHeap(this);
    owner->release();
}

}  // namespace kernel::device
