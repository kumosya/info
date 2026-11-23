/*
 * 记录显示布局，管理用户显存映射及其生命周期。
 */

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

namespace device {

    constexpr uint64_t pageSize = 4096;
    static ::FramebufferInfo bootFramebuffer{};
    static uint64_t framebufferCache = mm::pageCacheDisable;

    /*
     * 把地址向下对齐到包含它的页面起点。
     */
    static uint64_t alignDown(uint64_t value) {
        return value & ~(pageSize - 1);
    }

    /*
     * 把长度向上补齐到整页；本文件调用者在使用前负责检查长度和加法边界。
     */
    static uint64_t alignUp(uint64_t value) {
        return (value + pageSize - 1) & ~(pageSize - 1);
    }

    /*
     * 保存引导器提供的显示缓冲区信息，并向串口输出其布局，供后续显示服务映射。
     */
    void initDevices(const BootInfo* bootInfo) {
        bootFramebuffer = bootInfo == nullptr ? FramebufferInfo{}
                                              : bootInfo->getFramebuffer();
        const uint64_t bytes = uint64_t(bootFramebuffer.getPitch()) * bootFramebuffer.getHeight();
        const uint64_t cache = cpu::hasWriteCombining()
            ? mm::pageWriteCombining : mm::pageCacheDisable;
        if (bytes && bootFramebuffer.getAddress()) {
            if (!mm::configureFramebufferCache(bootFramebuffer.getAddress(), bytes, cache)) {
                device::serialWrite("[DISPLAY] framebuffer cache setup failed\n");
                cpu::halt();
            }

            framebufferCache = cache;
        }

        device::serialWrite(framebufferCache == mm::pageWriteCombining
            ? "[DISPLAY] framebuffer write combining enabled\n"
            : "[DISPLAY] framebuffer uncached fallback\n");
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

    /*
     * 检查显示能力和像素缓冲区范围，为任务建立不可执行的显存映射，缓存类型与内核别名一致。
     */
    bool mapFramebuffer(::task::Task& task, uint64_t userAddress,
                        services::wire::DisplayInfo& info, uint64_t* generation) {
        if (!userAddress) {
            userAddress = reserveDisplayAddress(64 * 1024 * 1024ULL);
        }

        // 显存映射只向有显示能力的任务开放，并要求用户地址页对齐及基本显示布局有效。
        if (!task.hasCapability(::task::taskCapabilityDisplay) ||
            bootFramebuffer.getAddress() == 0 ||
            bootFramebuffer.getWidth() == 0 ||
            bootFramebuffer.getHeight() == 0 ||
            bootFramebuffer.getPitch() == 0 ||
            bootFramebuffer.getBitsPerPixel() < 24 ||
            (userAddress & (pageSize - 1)) != 0 || userAddress < 0x10000) {
            device::serialWrite(
                "[DISPLAY] framebuffer mapping validation failed\n");

            return false;
        }

        // 显存起点可能落在页内：映射从页边界开始，但返回给程序的像素地址要加回这段偏移。
        const uint64_t physicalBase = alignDown(bootFramebuffer.getAddress());
        const uint64_t offset = bootFramebuffer.getAddress() - physicalBase;
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

        // 在地址空间锁内逐页建立同类型显存映射，不可执行，并保留外部所有权。
        ::task::PreemptionSpinLockGuard addressGuard(
            task.addressSpace().lock());
        uint64_t mapped = 0;

        for (; mapped < mappedSize; mapped += pageSize) {
            if (!mm::mapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    userAddress + mapped, physicalBase + mapped,
                    ::mm::pageUser | ::mm::pageWritable | ::mm::pageNoExecute |
                        framebufferCache | ::mm::pageExternal)) {
                device::serialWrite(
                    "[DISPLAY] framebuffer page mapping failed at ");
                device::serialWriteHex(mapped);
                device::serialWrite("\n");

                // 只撤销本次已经成功建立的页面，避免失败时留下半份显存映射。
                while (mapped != 0) {
                    mapped -= pageSize;
                    mm::unmapPageInAddressSpace(
                        task.addressSpace().rootPhysicalAddress(),
                        userAddress + mapped);
                }

                return false;
            }
        }

        // 保存当前映像代号，之后查询或销毁时用它识别 exec 前后的不同地址空间。
        if (generation) {
            *generation = task.addressSpace().generation();
        }

        return describeDisplay(userAddress, info);
    }

    /*
     * 把引导显示布局转换成用户可见的地址、大小与像素格式信息，不在这里建立映射。
     */
    bool describeDisplay(uint64_t userAddress, services::wire::DisplayInfo& info) {
        if (!bootFramebuffer.getAddress() || !bootFramebuffer.getPitch() ||
            !bootFramebuffer.getHeight()) {
            return false;
        }

        const uint64_t offset = bootFramebuffer.getAddress() & 4095;
        const uint64_t visibleSize =
            static_cast<uint64_t>(bootFramebuffer.getPitch()) *
            bootFramebuffer.getHeight();
        const uint64_t mappedSize = alignUp(offset + visibleSize);

        // 像素地址包含页内偏移，size 则描述整页映射范围，两者用途不同。
        info.address       = userAddress + offset;
        info.size          = mappedSize;
        info.width         = bootFramebuffer.getWidth();
        info.height        = bootFramebuffer.getHeight();
        info.pitch         = bootFramebuffer.getPitch();
        info.bitsPerPixel  = bootFramebuffer.getBitsPerPixel();
        info.redPosition   = bootFramebuffer.getRedPosition();
        info.greenPosition = bootFramebuffer.getGreenPosition();
        info.bluePosition  = bootFramebuffer.getBluePosition();

        // 只有识别出的颜色排列才标记为已知格式；其他布局保留原始位置信息供上层判断。
        if (bootFramebuffer.getBitsPerPixel() == 32 &&
            bootFramebuffer.getRedPosition() == 16 &&
            bootFramebuffer.getGreenPosition() == 8 &&
            bootFramebuffer.getBluePosition() == 0) {
            info.format = services::wire::PixelFormat::Bgra;
        } else if (bootFramebuffer.getBitsPerPixel() == 32 &&
                   bootFramebuffer.getRedPosition() == 0 &&
                   bootFramebuffer.getGreenPosition() == 8 &&
                   bootFramebuffer.getBluePosition() == 16) {
            info.format = services::wire::PixelFormat::Rgba;
        } else {
            info.format = services::wire::PixelFormat::Unknown;
        }

        return true;
    }

    /*
     * 创建显示映射包装，保存所属任务与映像代号并保留任务引用；实际映射失败时释放包装。
     */
    FrameMapping* FrameMapping::create(::task::Task& task, uint64_t address,
                                       services::wire::DisplayInfo& info) {
        void* storage = mm::allocateHeap(sizeof(FrameMapping));

        if (!storage) {
            return nullptr;
        }

        FrameMapping* mapping = new (storage) FrameMapping;

        if (!address) {
            address = reserveDisplayAddress(64 * 1024 * 1024ULL);
        }

        // 包装对象尚未交给调用者，映射失败直接释放；成功后保留任务引用，保证销毁时所有者仍存在。
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

    /*
     * 核对调用者、映像代号和逐页物理对应关系，只有原显示映射仍有效时才返回显示信息。
     */
    bool FrameMapping::query(::task::Task& task, services::wire::DisplayInfo& info) {
        if (owner_ != &task) {
            return false;
        }

        ::task::PreemptionSpinLockGuard guard(task.addressSpace().lock());

        // 原映像已被替换时旧句柄不能描述新映像里的同地址内容，因此先比较代号。
        if (task.addressSpace().generation() != generation_ ||
            !task.addressSpace().rootPhysicalAddress()) {
            return false;
        }

        // 再核对每页仍指向原来的显存物理地址，防止被撤销或被其他映射取代后继续使用。
        for (uint64_t offset = 0; offset < size_; offset += 4096) {
            if (mm::getPhysicalAddressInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    address_ + offset) !=
                (bootFramebuffer.getAddress() & ~4095ULL) + offset) {
                return false;
            }
        }

        return describeDisplay(address_, info);
    }

    /*
     * 只撤销仍属于原映像且指向原显示缓冲区的页面，再释放包装与任务引用，避免误删
     * exec 后复用同一地址的新映射。
     */
    void FrameMapping::destroy() {
        ::task::Task* owner = owner_;
        {
            ::task::PreemptionSpinLockGuard guard(owner->addressSpace().lock());

            // 只有代号与物理页都吻合才撤销，不能误删 exec
            // 后复用了相同虚拟地址的新映射。
            if (owner->addressSpace().generation() == generation_ &&
                owner->addressSpace().rootPhysicalAddress()) {
                for (uint64_t offset = 0; offset < size_; offset += 4096) {
                    if (mm::getPhysicalAddressInAddressSpace(
                            owner->addressSpace().rootPhysicalAddress(),
                            address_ + offset) ==
                        (bootFramebuffer.getAddress() & ~4095ULL) + offset) {
                        mm::unmapPageInAddressSpace(
                            owner->addressSpace().rootPhysicalAddress(),
                            address_ + offset);
                    }
                }
            }
        }

        // 页表操作结束并离开地址空间锁后，再释放包装对象及其持有的任务引用。
        mm::freeHeap(this);
        owner->release();
    }

}  // namespace device
