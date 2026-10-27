#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/pci.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"

namespace kernel::device {

namespace {
constexpr uint32_t capacity = 32, maximumDmaPages = 2048;
constexpr uint64_t controlBase = 0xffff807000000000ULL;
ResourceDomains domainRegistry;

bool same(const os::wire::PciFunctionInfo& a,
          const os::wire::PciFunctionInfo& b) {
    return a.segment == b.segment && a.bus == b.bus && a.device == b.device &&
           a.function == b.function;
}
}  // namespace

ResourceDomains& resourceDomains() { return domainRegistry; }
void ResourceDomains::init() {
    worker = ::kernel::task::Thread::create(
        nullptr,
        [](void* context) { static_cast<ResourceDomains*>(context)->work(); },
        this);
    if (!worker) {
        device::serialWrite("[DEVICE] cannot create resource reclaimer\n");
        cpu::halt();
    }
}
bool ResourceDomains::owned(::kernel::task::Task& owner) {
    ::kernel::task::InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains)
        if (domain.owner_ == &owner) return true;
    return false;
}
ResourceDomains::Statistics ResourceDomains::statistics() {
    Statistics result{};
    ::kernel::task::InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains) {
        if (domain.state_ == State::Active) ++result.active;
        if (domain.state_ == State::Pending || domain.state_ == State::Stopping)
            ++result.pending;
        if (domain.state_ == State::Stopped) ++result.stopped;
        if (domain.state_ == State::Quarantined) ++result.quarantined;
        result.pages += domain.dmaPages_;
    }
    return result;
}

ResourceDomain* ResourceDomains::claim(
    ::kernel::task::Task& owner, const os::wire::PciFunctionInfo& function) {
    if (owner.serviceRole() != os::ServiceRole::Storage ||
        !owner.hasCapability(::kernel::task::taskCapabilityDevice))
        return nullptr;
    ::kernel::task::PreemptionSpinLockGuard guard(domainLock);
    ResourceDomain* domain = nullptr;
    uint32_t slot          = 0;
    for (uint32_t i = 0; i < capacity; ++i) {
        if (domains[i].state_ != State::Free &&
            same(domains[i].function_, function)) {
            if (domains[i].state_ == State::Quarantined &&
                domains[i].exiting_) {
                domains[i].state_ = State::Pending;
                ::kernel::task::wakeThread(worker);
            }
            return nullptr;
        }
        if (!domain && domains[i].state_ == State::Free) {
            domain = &domains[i];
            slot   = i;
        }
    }
    uint32_t identity = 0, command = 0, capabilities = 0;
    // Only the currently supported modern VirtIO block transport has a trusted
    // reset recipe.
    if (!domain || !Pci(pciBus(), function).read(0, identity) ||
        identity != 0x10421af4 || !Pci(pciBus(), function).read(4, command) ||
        !Pci(pciBus(), function).write(4, (command & 0xffff) & ~4U) ||
        !Pci(pciBus(), function).read(0x34, capabilities))
        return nullptr;
    uint8_t position = capabilities & 0xff;
    uint64_t barBases[6]{}, barSizes[6]{};
    for (uint8_t bar = 0; bar < 6; ++bar) {
        uint32_t raw = 0;
        if (!Pci(pciBus(), function).read(0x10 + bar * 4, raw)) return nullptr;
        if (!(raw & 1))
            Pci(pciBus(), function)
                .getMemoryBar(bar, barBases[bar], barSizes[bar]);
        if (!(raw & 1) && ((raw >> 1) & 3) == 2) ++bar;
    }
    uint64_t statusPhysical = 0;
    for (unsigned n = 0; position && n < 48; ++n) {
        uint32_t header = 0, type = 0, offset = 0, length = 0;
        if (position < 0x40 || position > 0xec || (position & 3) ||
            !Pci(pciBus(), function).read(position, header))
            return nullptr;
        const uint8_t next = (header >> 8) & 0xff;
        if ((header & 0xff) == 9 && (header >> 24) == 1 &&
            ((header >> 16) & 0xff) >= 16) {
            if (!Pci(pciBus(), function).read(position + 4, type) ||
                !Pci(pciBus(), function).read(position + 8, offset) ||
                !Pci(pciBus(), function).read(position + 12, length) ||
                length < 56 || (type & 0xff) >= 6)
                return nullptr;
            const uint64_t base = barBases[type & 0xff],
                           size = barSizes[type & 0xff];
            if (!base || offset > size || 56 > size - offset ||
                base > UINT64_MAX - offset - 20)
                return nullptr;
            statusPhysical = base + offset + 20;
            break;
        }
        if (next == position) return nullptr;
        position = next;
    }
    if (!statusPhysical) return nullptr;
    const uint64_t mapped = controlBase + slot * 4096ULL;
    if (!mm::mapPage(mapped, statusPhysical & ~4095ULL,
                     ::kernel::mm::pageWritable | ::kernel::mm::pageNoExecute |
                         ::kernel::mm::pageCacheDisable |
                         ::kernel::mm::pageExternal))
        return nullptr;
    owner.retain();
    domain->owner_             = &owner;
    domain->function_          = function;
    domain->references_        = 1;
    domain->addressGeneration_ = owner.addressSpace().generation();
    domain->controlMapping_    = mapped;
    domain->controlSize_       = 4096;
    domain->status_ =
        reinterpret_cast<volatile uint8_t*>(mapped + (statusPhysical & 4095));
    for (unsigned bar = 0; bar < 6; ++bar) {
        domain->barBases_[bar] = barBases[bar];
        domain->barSizes_[bar] = barSizes[bar];
    }
    domain->state_ = State::Active;
    return domain;
}

ResourceDomain* ResourceDomains::find(
    ::kernel::task::Task& owner, const os::wire::PciFunctionInfo& function) {
    ::kernel::task::InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains)
        if (domain.owner_ == &owner && domain.state_ == State::Active &&
            same(domain.function_, function)) {
            ++domain.references_;
            return &domain;
        }
    return nullptr;
}
bool ResourceDomain::active(::kernel::task::Task& owner) {
    ::kernel::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);
    return owner_ == &owner && state_ == State::Active;
}
bool ResourceDomains::writeConfiguration(
    uint64_t owner, const os::wire::PciFunctionInfo& function, uint16_t offset,
    uint32_t value) {
    ::kernel::task::InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains)
        if (domain.state_ != State::Free && same(domain.function_, function)) {
            // Claimed BARs/capabilities are immutable. Command writes cannot
            // restart an old domain.
            return domain.owner_ && domain.owner_->id() == owner &&
                   domain.state_ == State::Active && offset == 4 &&
                   Pci(pciBus(), function).write(offset, value & 0xffff);
        }
    // A PCI proxy cannot enable DMA for a function without a resource owner.
    return offset == 4 && !(value & 4) &&
           Pci(pciBus(), function).write(offset, value & 0xffff);
}
bool ResourceDomain::attach(MmioRegion& region) {
    ::kernel::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);
    if (state_ != State::Active) return false;
    region.domain_ = this;
    region.next_   = mmio_;
    mmio_          = &region;
    ++references_;
    return true;
}
bool ResourceDomain::attach(DmaBuffer& buffer) {
    ::kernel::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);
    if (state_ != State::Active ||
        buffer.size_ / 4096 > maximumDmaPages - dmaPages_)
        return false;
    buffer.domain_ = this;
    buffer.next_   = dma_;
    dma_           = &buffer;
    ++references_;
    dmaPages_ += buffer.size_ / 4096;
    return true;
}
void ResourceDomain::drop() {
    ::kernel::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);
    if (references_) --references_;
    ::kernel::task::wakeThread(resourceDomains().worker);
}
void ResourceDomain::closeLease() {
    ::kernel::task::InterruptSpinLockGuard guard(resourceDomains().domainLock);
    if (state_ == State::Active) state_ = State::Pending;
    if (references_) --references_;
    ::kernel::task::wakeThread(resourceDomains().worker);
}
void ResourceDomains::ownerExiting(::kernel::task::Task& owner) {
    ::kernel::task::InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains)
        if (domain.owner_ == &owner) {
            domain.exiting_ = true;
            if (domain.state_ == State::Active) domain.state_ = State::Pending;
        }
    ::kernel::task::wakeThread(worker);
}
void ResourceDomains::addressSpaceReleased(::kernel::task::Task& owner) {
    ::kernel::task::InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains)
        if (domain.owner_ == &owner) domain.addressSpaceGone_ = true;
    ::kernel::task::wakeThread(worker);
}
void ResourceDomain::unmap(uint64_t address, uint64_t size, bool& mapped,
                           uint64_t physical) {
    ::kernel::task::PreemptionSpinLockGuard guard(
        owner_->addressSpace().lock());
    if (!__atomic_exchange_n(&mapped, false, __ATOMIC_ACQ_REL)) return;
    const uint64_t root = owner_->addressSpace().rootPhysicalAddress();
    if (root && owner_->addressSpace().generation() == addressGeneration_)
        for (uint64_t offset = 0; offset < size; offset += 4096)
            if (mm::getPhysicalAddressInAddressSpace(root, address + offset) ==
                physical + offset)
                mm::unmapPageInAddressSpace(root, address + offset);
}
bool ResourceDomain::reset() {
    for (MmioRegion* region = mmio_; region; region = region->next_)
        unmap(region->virtualAddress_, region->size_, region->mapped_,
              region->physicalAddress_);
    uint32_t command = 0;
    if (!Pci(pciBus(), function_).read(4, command) ||
        !Pci(pciBus(), function_).write(4, (command & 0xffff) & ~4U))
        return false;

    *status_ = 0;
    asm volatile("mfence" ::: "memory");
    const uint64_t deadline = time::getMonotonicTime() + 100000000ULL;
    while (*status_ != 0) {
        if (time::getMonotonicTime() >= deadline) return false;
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
    const uint64_t deadline = time::getMonotonicTime() + 2000000000ULL;
    while (true) {
        {
            ::kernel::task::InterruptSpinLockGuard guard(
                resourceDomains().domainLock);
            if (state_ == State::Stopped) return 0;
            if (state_ == State::Quarantined) return -11;
        }
        if (time::getMonotonicTime() >= deadline) return -9;
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
        // All handles are gone and all user mappings have been revoked or
        // destroyed.
        ::kernel::task::Task* owner = pending->owner_;
        for (DmaBuffer* buffer = pending->dma_; buffer;) {
            DmaBuffer* next = buffer->next_;
            uint32_t order  = 0;
            while ((4096ULL << order) < buffer->size_) ++order;
            mm::releasePhysicalBlock(buffer->physicalAddress_, order,
                                     owner->id());
            mm::freeHeap(buffer);
            buffer = next;
        }
        for (MmioRegion* region = pending->mmio_; region;) {
            MmioRegion* next = region->next_;
            mm::freeHeap(region);
            region = next;
        }
        mm::unmapPage(pending->controlMapping_);
        {
            ::kernel::task::InterruptSpinLockGuard guard(domainLock);
            *pending = ResourceDomain{};
        }
        owner->release();
        device::serialWrite("[DEVICE] DMA domain reclaimed\n");
    }
}

bool ResourceDomain::allowsIrq(::kernel::task::Task& owner, uint8_t irq) {
    if (!active(owner)) return false;
    uint32_t value = 0;
    return Pci(pciBus(), function_).read(0x3c, value) && (value & 0xff) == irq;
}

}  // namespace kernel::device
