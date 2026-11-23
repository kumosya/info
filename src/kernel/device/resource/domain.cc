/*
 * 管理设备所有权、停机与 DMA 页面延迟回收。
 */

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/pci.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"

namespace device {

    constexpr uint32_t capacity = 32, maximumDmaPages = 2048;
    constexpr uint64_t controlBase = 0xffff807000000000ULL;
    static ResourceDomains domainRegistry;

    /*
     * 比较 PCI 设备的稳定定位字段，区分同一设备不同功能。
     */
    static bool same(const os::wire::PciFunctionInfo& a,
                     const os::wire::PciFunctionInfo& b) {
        return a.segment == b.segment && a.bus == b.bus &&
               a.device == b.device && a.function == b.function;
    }

    /*
     * 返回统一设备资源域管理器，所有设备映射与 DMA 生命周期在这里协调。
     */
    ResourceDomains& resourceDomains() { return domainRegistry; }

    /*
     * 启动设备资源后台回收线程；无法建立回收能力时停机，避免后续设备退出无人处理。
     */
    void ResourceDomains::init() {
        worker = ::task::Thread::create(
            nullptr,
            [](void* context) {
                static_cast<ResourceDomains*>(context)->work();
            },
            this);

        if (!worker) {
            device::serialWrite("[DEVICE] cannot create resource reclaimer\n");
            cpu::halt();
        }
    }

    /*
     * 在资源锁内检查任务是否仍拥有任何设备域，供 fork 等操作限制硬件资源继承。
     */
    bool ResourceDomains::owned(::task::Task& owner) {
        ::task::InterruptSpinLockGuard guard(domainLock);

        for (ResourceDomain& domain : domains) {
            if (domain.owner_ == &owner) {
                return true;
            }
        }

        return false;
    }

    /*
     * 在资源锁内汇总活动、等待停止、已停止和隔离域数量，以及占用的 DMA 页数。
     */
    ResourceDomains::Statistics ResourceDomains::statistics() {
        Statistics result{};
        ::task::InterruptSpinLockGuard guard(domainLock);

        for (ResourceDomain& domain : domains) {
            if (domain.state_ == State::Active) {
                ++result.active;
            }

            if (domain.state_ == State::Pending ||
                domain.state_ == State::Stopping) {
                ++result.pending;
            }

            if (domain.state_ == State::Stopped) {
                ++result.stopped;
            }

            if (domain.state_ == State::Quarantined) {
                ++result.quarantined;
            }

            result.pages += domain.dmaPages_;
        }

        return result;
    }

    /*
     * 为受允许的存储服务认领一个受支持设备，验证 BAR
     * 和复位控制位置后建立独占资源域；已有未回收域时拒绝重复认领。
     */
    ResourceDomain* ResourceDomains::claim(
        ::task::Task& owner, const os::wire::PciFunctionInfo& function) {
        // 当前只允许具有设备能力的固定存储服务认领，避免任意任务拿到直接控制硬件的权限。
        if (owner.serviceRole() != os::ServiceRole::Storage ||
            !owner.hasCapability(::task::taskCapabilityDevice)) {
            return nullptr;
        }

        ::task::PreemptionSpinLockGuard guard(domainLock);
        ResourceDomain* domain = nullptr;
        uint32_t slot          = 0;

        // 同一 PCI
        // 功能只能属于一个资源域；扫描时一并寻找空槽，隔离中的旧域不能直接重新认领。
        for (uint32_t i = 0; i < capacity; ++i) {
            if (domains[i].state_ != State::Free &&
                same(domains[i].function_, function)) {
                if (domains[i].state_ == State::Quarantined &&
                    domains[i].exiting_) {
                    domains[i].state_ = State::Pending;
                    ::task::wakeThread(worker);
                }

                return nullptr;
            }

            if (!domain && domains[i].state_ == State::Free) {
                domain = &domains[i];
                slot   = i;
            }
        }

        uint32_t identity = 0, command = 0, capabilities = 0;
        // 当前只有现代 VirtIO
        // 块设备具有内核认可的复位流程，其他硬件暂不允许认领。
        if (!domain || !Pci(pciBus(), function).read(0, identity) ||
            identity != 0x10421af4 ||
            !Pci(pciBus(), function).read(4, command) ||
            !Pci(pciBus(), function).write(4, (command & 0xffff) & ~4U) ||
            !Pci(pciBus(), function).read(0x34, capabilities)) {
            return nullptr;
        }

        // 保存各内存 BAR
        // 的边界，后面验证控制寄存器是否确实落在设备声明的窗口中。
        uint8_t position = capabilities & 0xff;
        uint64_t barBases[6]{}, barSizes[6]{};

        for (uint8_t bar = 0; bar < 6; ++bar) {
            uint32_t raw = 0;

            if (!Pci(pciBus(), function).read(0x10 + bar * 4, raw)) {
                return nullptr;
            }

            if (!(raw & 1)) {
                Pci(pciBus(), function)
                    .getMemoryBar(bar, barBases[bar], barSizes[bar]);
            }

            if (!(raw & 1) && ((raw >> 1) & 3) == 2) {
                ++bar;
            }
        }

        uint64_t statusPhysical = 0;

        // 能力链由设备给出，限制遍历次数并检查对齐与范围，避免损坏指针形成无限循环或越界读取。
        for (unsigned n = 0; position && n < 48; ++n) {
            uint32_t header = 0, type = 0, offset = 0, length = 0;

            if (position < 0x40 || position > 0xec || (position & 3) ||
                !Pci(pciBus(), function).read(position, header)) {
                return nullptr;
            }

            const uint8_t next = (header >> 8) & 0xff;

            if ((header & 0xff) == 9 && (header >> 24) == 1 &&
                ((header >> 16) & 0xff) >= 16) {
                if (!Pci(pciBus(), function).read(position + 4, type) ||
                    !Pci(pciBus(), function).read(position + 8, offset) ||
                    !Pci(pciBus(), function).read(position + 12, length) ||
                    length < 56 || (type & 0xff) >= 6) {
                    return nullptr;
                }

                const uint64_t base = barBases[type & 0xff],
                               size = barSizes[type & 0xff];

                if (!base || offset > size || 56 > size - offset ||
                    base > UINT64_MAX - offset - 20) {
                    return nullptr;
                }

                // 现代 VirtIO 公共配置的第 20
                // 字节是设备状态；后台通过写零执行受支持的复位流程。
                statusPhysical = base + offset + 20;
                break;
            }

            if (next == position) {
                return nullptr;
            }

            position = next;
        }

        if (!statusPhysical) {
            return nullptr;
        }

        // 每个资源域保留一个仅供内核使用的控制页映射，用户映射撤销后仍能操作停机寄存器。
        const uint64_t mapped = controlBase + slot * 4096ULL;

        if (!mm::mapPage(mapped, statusPhysical & ~4095ULL,
                         ::mm::pageWritable | ::mm::pageNoExecute |
                             ::mm::pageCacheDisable | ::mm::pageExternal)) {
            return nullptr;
        }

        // 所有检查成功才交接归属：保留任务引用、记录映像代号与初始租约引用，最后发布
        // Active 状态。
        owner.retain();
        domain->owner_             = &owner;
        domain->function_          = function;
        domain->references_        = 1;
        domain->addressGeneration_ = owner.addressSpace().generation();
        domain->controlMapping_    = mapped;
        domain->controlSize_       = 4096;
        domain->status_            = reinterpret_cast<volatile uint8_t*>(
            mapped + (statusPhysical & 4095));

        for (unsigned bar = 0; bar < 6; ++bar) {
            domain->barBases_[bar] = barBases[bar];
            domain->barSizes_[bar] = barSizes[bar];
        }

        domain->state_ = State::Active;

        return domain;
    }

    /*
     * 查找任务拥有的活动设备域，找到后增加一份临时引用，调用者使用结束须归还。
     */
    ResourceDomain* ResourceDomains::find(
        ::task::Task& owner, const os::wire::PciFunctionInfo& function) {
        ::task::InterruptSpinLockGuard guard(domainLock);

        for (ResourceDomain& domain : domains) {
            if (domain.owner_ == &owner && domain.state_ == State::Active &&
                same(domain.function_, function)) {
                // 查找返回的指针自带一份临时引用，调用者用完必须
                // drop；长期映射通过 attach 另持引用。
                ++domain.references_;

                return &domain;
            }
        }

        return nullptr;
    }

    /*
     * 核对资源域仍处于活动状态且属于指定任务。
     */
    bool ResourceDomain::active(::task::Task& owner) {
        ::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);

        return owner_ == &owner && state_ == State::Active;
    }

    /*
     * 限制受管理设备的配置修改：认领后的 BAR
     * 和能力布局不可改，命令寄存器写入必须符合当前所有权及停机状态。
     */
    bool ResourceDomains::writeConfiguration(
        uint64_t owner, const os::wire::PciFunctionInfo& function,
        uint16_t offset, uint32_t value) {
        ::task::InterruptSpinLockGuard guard(domainLock);

        for (ResourceDomain& domain : domains) {
            if (domain.state_ != State::Free &&
                same(domain.function_, function)) {
                // 设备认领后 BAR
                // 与能力布局固定；只允许活动所有者修改命令寄存器，不能重新启动旧资源域。
                return domain.owner_ && domain.owner_->id() == owner &&
                       domain.state_ == State::Active && offset == 4 &&
                       Pci(pciBus(), function).write(offset, value & 0xffff);
            }
        }

        // 没有资源所有者的设备，PCI 代理也不能替它开启 DMA。
        return offset == 4 && !(value & 4) &&
               Pci(pciBus(), function).write(offset, value & 0xffff);
    }

    /*
     * 把 MMIO 包装加入活动域，并增加资源引用，使域在包装关闭前不能回收。
     */
    bool ResourceDomain::attach(MmioRegion& region) {
        ::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);

        if (state_ != State::Active) {
            return false;
        }

        // 活动状态下才把对象接到域的链表；增加的长期引用由该对象关闭时归还。
        region.domain_ = this;
        region.next_   = mmio_;
        mmio_          = &region;
        ++references_;

        return true;
    }

    /*
     * 把 DMA 缓冲区加入活动域并检查总页数上限，成功后记录引用与占用。
     */
    bool ResourceDomain::attach(DmaBuffer& buffer) {
        ::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);

        // 除了活动状态，还限制域累计占用的 DMA 页数，避免一个设备耗尽物理内存。
        if (state_ != State::Active ||
            buffer.size_ / 4096 > maximumDmaPages - dmaPages_) {
            return false;
        }

        // 域同时记住物理块对象和引用数，后面即使用户句柄关闭也能找到待停机回收的内存。
        buffer.domain_ = this;
        buffer.next_   = dma_;
        dma_           = &buffer;
        ++references_;
        dmaPages_ += buffer.size_ / 4096;

        return true;
    }

    /*
     * 归还一份资源域引用，并唤醒后台线程重新判断能否回收。
     */
    void ResourceDomain::drop() {
        ::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);

        if (references_) {
            --references_;
        }

        ::task::wakeThread(resourceDomains().worker);
    }

    /*
     * 关闭设备域句柄对应的使用资格，将活动域改为等待停机并唤醒回收线程。
     */
    void ResourceDomain::closeLease() {
        ::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);

        // 最后一个设备租约关闭时先申请停机；引用归零也不能跳过停机直接释放 DMA
        // 页面。
        if (state_ == State::Active) {
            state_ = State::Pending;
        }

        if (references_) {
            --references_;
        }

        ::task::wakeThread(resourceDomains().worker);
    }

    /*
     * 标记任务正在退出，把其活动设备域提交给后台停机流程，页面此时仍需保留。
     */
    void ResourceDomains::ownerExiting(::task::Task& owner) {
        ::task::InterruptSpinLockGuard guard(domainLock);

        // 标记所有属于该任务的域等待停机；退出不等于硬件已停，后续还要等地址空间释放通知。
        for (ResourceDomain& domain : domains) {
            if (domain.owner_ == &owner) {
                domain.exiting_ = true;

                if (domain.state_ == State::Active) {
                    domain.state_ = State::Pending;
                }
            }
        }

        ::task::wakeThread(worker);
    }

    /*
     * 通知资源域任务旧地址空间已经释放，后台可结合设备状态和剩余引用决定最终回收。
     */
    void ResourceDomains::addressSpaceReleased(::task::Task& owner) {
        ::task::InterruptSpinLockGuard guard(domainLock);

        for (ResourceDomain& domain : domains) {
            if (domain.owner_ == &owner) {
                domain.addressSpaceGone_ = true;
            }
        }

        ::task::wakeThread(worker);
    }

    /*
     * 原子撤销一次资源映射，只在原映像代号与物理对应关系仍匹配时解除页表，避免误操作新映像。
     */
    void ResourceDomain::unmap(uint64_t address, uint64_t size, bool& mapped,
                               uint64_t physical) {
        ::task::PreemptionSpinLockGuard guard(owner_->addressSpace().lock());

        // 原子领取撤销工作，避免用户关闭与后台停机对同一对象重复撤销。
        if (!__atomic_exchange_n(&mapped, false, __ATOMIC_ACQ_REL)) {
            return;
        }

        const uint64_t root = owner_->addressSpace().rootPhysicalAddress();

        // 代号不符说明 exec
        // 已换映像；即使代号相同也逐页比对物理地址，保护同地址的新映射。
        if (root && owner_->addressSpace().generation() == addressGeneration_) {
            for (uint64_t offset = 0; offset < size; offset += 4096) {
                if (mm::getPhysicalAddressInAddressSpace(
                        root, address + offset) == physical + offset) {
                    mm::unmapPageInAddressSpace(root, address + offset);
                }
            }
        }
    }

    /*
     * 撤销 MMIO 用户访问并关闭 PCI
     * 总线主控，写设备复位状态后有限期等待确认，确保设备不会继续使用即将归还的
     * DMA 页。
     */
    bool ResourceDomain::reset() {
        // 先撤掉用户 MMIO，阻止驱动在内核停机过程中继续改设备寄存器。
        for (MmioRegion* region = mmio_; region; region = region->next_) {
            unmap(region->virtualAddress_, region->size_, region->mapped_,
                  region->physicalAddress_);
        }

        // 关闭 PCI 总线主控位，先阻止设备发起新的
        // DMA，再通过受信任的状态寄存器发起复位。
        uint32_t command = 0;

        if (!Pci(pciBus(), function_).read(4, command) ||
            !Pci(pciBus(), function_).write(4, (command & 0xffff) & ~4U)) {
            return false;
        }

    *status_ = 0;
    asm volatile("mfence" ::: "memory");
    const uint64_t start = time::getMonotonicTime();
    while (*status_ != 0) {
        if (time::getMonotonicTime() - start >= 100000000ULL) return false;
        task::yieldThread();
    }
    return Pci(pciBus(), function_).read(4, command) && !(command & 4);
}
int ResourceDomain::stop(::kernel::task::Task& owner) {
    {
        ::kernel::task::InterruptSpinLockGuard guard(
            resourceDomains().domainLock);
        if (owner_ != &owner) return -3;
        if (state_ == State::Active) state_ = State::Pending;
        ::kernel::task::wakeThread(resourceDomains().worker);
    }
    const uint64_t start = time::getMonotonicTime();
    while (true) {
        {
            ::kernel::task::InterruptSpinLockGuard guard(
                resourceDomains().domainLock);
            if (state_ == State::Stopped) return 0;
            if (state_ == State::Quarantined) return -11;
        }
        if (time::getMonotonicTime() - start >= 2000000000ULL) return -9;
        task::yieldThread();
    }
}
void ResourceDomains::work() {
    while (true) {
        ResourceDomain* pending = nullptr;
        {
            ::kernel::task::InterruptSpinLockGuard guard(domainLock);
            for (auto& domain : domains) {
                if (domain.state_ == State::Pending) {
                    pending       = &domain;
                    domain.state_ = State::Stopping;
                    break;
                }
                if (domain.state_ == State::Stopped && !domain.references_ &&
                    (!domain.exiting_ || domain.addressSpaceGone_)) {
                    pending = &domain;
                    break;
                }
            }
            if (!pending) ::kernel::task::prepareCurrentWait();
        }
        if (!pending) {
            ::kernel::task::blockWaitingThread();
            continue;
        }
        if (pending->state_ == State::Stopping) {
            const bool stopped = pending->reset();
            {
                ::kernel::task::InterruptSpinLockGuard guard(domainLock);
                pending->state_ = stopped ? State::Stopped : State::Quarantined;
            }
            device::serialWrite(
                stopped ? "[DEVICE] DMA domain stopped\n"
                        : "[DEVICE] DMA domain quarantined; pages retained\n");
                continue;
            }

            // 所有句柄均已关闭，用户映射也已撤销或随地址空间销毁，现在才能回收设备后备内存。
            ::task::Task* owner = pending->owner_;

            // 到这里才释放连续物理块和其包装对象；已关闭句柄的对象仍由资源域链表持有。
            for (DmaBuffer* buffer = pending->dma_; buffer;) {
                DmaBuffer* next = buffer->next_;
                uint32_t order  = 0;

                while ((4096ULL << order) < buffer->size_) {
                    ++order;
                }

                mm::releasePhysicalBlock(buffer->physicalAddress_, order,
                                         owner->id());
                mm::freeHeap(buffer);
                buffer = next;
            }

            // MMIO 包装也延迟到这里统一释放，随后撤销内核专用控制映射。
            for (MmioRegion* region = pending->mmio_; region;) {
                MmioRegion* next = region->next_;
                mm::freeHeap(region);
                region = next;
            }

            mm::unmapPage(pending->controlMapping_);
            {
                ::task::InterruptSpinLockGuard guard(domainLock);

                // 锁内清空槽使它重新可认领，锁外归还任务引用，避免析构路径与资源锁互相等待。
                *pending = ResourceDomain{};
            }

            owner->release();
            device::serialWrite("[DEVICE] DMA domain reclaimed\n");
        }
    }

    /*
     * 检查任务仍拥有活动域，并确认请求 IRQ 与该设备配置记录的中断线一致。
     */
    bool ResourceDomain::allowsIrq(::task::Task& owner, uint8_t irq) {
        if (!active(owner)) {
            return false;
        }

        uint32_t value = 0;

        return Pci(pciBus(), function_).read(0x3c, value) &&
               (value & 0xff) == irq;
    }

}  // namespace device
