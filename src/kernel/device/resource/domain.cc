#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "kernel/time.h"
#include "kernel/serial.h"

namespace kernel::device {
namespace {
constexpr uint32_t capacity = 32, maximumDmaPages = 2048;
constexpr uint64_t controlBase = 0xffff807000000000ULL;
ResourceDomain domains[capacity];
SpinLock domainLock;
Thread* worker;
#ifdef OS_DEVICE_RESOURCE_TEST
bool blockReset;
#endif
bool same(const os::PciFunctionInfo& a, const os::PciFunctionInfo& b) {
    return a.segment == b.segment && a.bus == b.bus && a.device == b.device && a.function == b.function;
}
}

void ResourceDomain::initialize() {
    worker = Thread::create(nullptr, work, nullptr);
    if (!worker) { serial::serialWrite("[DEVICE] cannot create resource reclaimer\n"); cpu::halt(); }
}
bool ResourceDomain::owned(Task& owner) {
    InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains) if (domain.owner_ == &owner) return true;
    return false;
}
ResourceDomain::Statistics ResourceDomain::statistics() {
    Statistics result{};
    InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains) {
        if (domain.state_ == State::Active) ++result.active;
        if (domain.state_ == State::Pending || domain.state_ == State::Stopping) ++result.pending;
        if (domain.state_ == State::Stopped) ++result.stopped;
        if (domain.state_ == State::Quarantined) ++result.quarantined;
        result.pages += domain.dmaPages_;
    }
    return result;
}
#ifdef OS_DEVICE_RESOURCE_TEST
void ResourceDomain::testResetFailure(bool enabled) { __atomic_store_n(&blockReset, enabled, __ATOMIC_RELEASE); }
bool ResourceDomain::testQuarantinePinned() {
    InterruptSpinLockGuard guard(domainLock);
    bool found = false;
    for (auto& domain : domains) if (domain.state_ == State::Quarantined) {
        found = true;
        for (DmaBuffer* buffer = domain.dma_; buffer; buffer = buffer->next_) {
            memory::PhysicalPageInfo info{};
            if (!memory::queryPhysicalPage(buffer->physicalAddress_, info) || info.state != memory::FrameState::Allocated || !info.references) return false;
        }
    }
    return found;
}
#endif

ResourceDomain* ResourceDomain::claim(Task& owner, const os::PciFunctionInfo& function) {
    if (owner.serviceRole() != os::ServiceRole::Storage || !owner.hasCapability(taskCapabilityDevice)) return nullptr;
    PreemptionSpinLockGuard guard(domainLock);
    ResourceDomain* domain = nullptr;
    uint32_t slot = 0;
    for (uint32_t i = 0; i < capacity; ++i) {
        if (domains[i].state_ != State::Free && same(domains[i].function_, function)) {
            if (domains[i].state_ == State::Quarantined && domains[i].exiting_) { domains[i].state_ = State::Pending; Thread::wake(worker); }
            return nullptr;
        }
        if (!domain && domains[i].state_ == State::Free) { domain = &domains[i]; slot = i; }
    }
    uint32_t identity = 0, command = 0, capabilities = 0;
    // Only the currently supported modern VirtIO block transport has a trusted reset recipe.
    if (!domain || !PciAccess::read(function, 0, identity) || identity != 0x10421af4 ||
        !PciAccess::read(function, 4, command) || !PciAccess::write(function, 4, (command & 0xffff) & ~4U) ||
        !PciAccess::read(function, 0x34, capabilities)) return nullptr;
    uint8_t position = capabilities & 0xff;
    uint64_t barBases[6]{}, barSizes[6]{};
    for (uint8_t bar = 0; bar < 6; ++bar) {
        uint32_t raw = 0;
        if (!PciAccess::read(function, 0x10 + bar * 4, raw)) return nullptr;
        if (!(raw & 1)) PciAccess::getMemoryBar(function, bar, barBases[bar], barSizes[bar]);
        if (!(raw & 1) && ((raw >> 1) & 3) == 2) ++bar;
    }
    uint64_t statusPhysical = 0;
    for (unsigned n = 0; position && n < 48; ++n) {
        uint32_t header = 0, type = 0, offset = 0, length = 0;
        if (position < 0x40 || position > 0xec || (position & 3) || !PciAccess::read(function, position, header)) return nullptr;
        const uint8_t next = (header >> 8) & 0xff;
        if ((header & 0xff) == 9 && (header >> 24) == 1 && ((header >> 16) & 0xff) >= 16) {
            if (!PciAccess::read(function, position + 4, type) || !PciAccess::read(function, position + 8, offset) ||
                !PciAccess::read(function, position + 12, length) || length < 56 || (type & 0xff) >= 6) return nullptr;
            const uint64_t base = barBases[type & 0xff], size = barSizes[type & 0xff];
            if (!base || offset > size || 56 > size - offset || base > UINT64_MAX - offset - 20) return nullptr;
            statusPhysical = base + offset + 20;
            break;
        }
        if (next == position) return nullptr;
        position = next;
    }
    if (!statusPhysical) return nullptr;
    const uint64_t mapped = controlBase + slot * 4096ULL;
    if (!memory::mapPage(mapped, statusPhysical & ~4095ULL, pageWritable | pageNoExecute | pageCacheDisable | pageExternal)) return nullptr;
    owner.retain();
    domain->owner_ = &owner; domain->function_ = function; domain->references_ = 1;
    domain->addressGeneration_ = owner.addressSpace().generation();
    domain->controlMapping_ = mapped; domain->controlSize_ = 4096;
    domain->status_ = reinterpret_cast<volatile uint8_t*>(mapped + (statusPhysical & 4095));
    for (unsigned bar = 0; bar < 6; ++bar) { domain->barBases_[bar] = barBases[bar]; domain->barSizes_[bar] = barSizes[bar]; }
    domain->state_ = State::Active;
    return domain;
}

ResourceDomain* ResourceDomain::find(Task& owner, const os::PciFunctionInfo& function) {
    InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains) if (domain.owner_ == &owner && domain.state_ == State::Active && same(domain.function_, function)) { ++domain.references_; return &domain; }
    return nullptr;
}
bool ResourceDomain::active(Task& owner) {
    InterruptSpinLockGuard guard(domainLock);
    return owner_ == &owner && state_ == State::Active;
}
bool ResourceDomain::writeConfiguration(uint64_t owner, const os::PciFunctionInfo& function, uint16_t offset, uint32_t value) {
    InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains) if (domain.state_ != State::Free && same(domain.function_, function)) {
        // Claimed BARs/capabilities are immutable. Command writes cannot restart an old domain.
        return domain.owner_ && domain.owner_->id() == owner && domain.state_ == State::Active && offset == 4 &&
            PciAccess::write(function, offset, value & 0xffff);
    }
    // A PCI proxy cannot enable DMA for a function without a resource owner.
    return offset == 4 && !(value & 4) && PciAccess::write(function, offset, value & 0xffff);
}
bool ResourceDomain::attach(MmioRegion& region) {
    InterruptSpinLockGuard guard(domainLock);
    if (state_ != State::Active) return false;
    region.domain_ = this; region.next_ = mmio_; mmio_ = &region;
    ++references_; return true;
}
bool ResourceDomain::attach(DmaBuffer& buffer) {
    InterruptSpinLockGuard guard(domainLock);
    if (state_ != State::Active || dmaPages_ == maximumDmaPages) return false;
    buffer.domain_ = this; buffer.next_ = dma_; dma_ = &buffer;
    ++references_; ++dmaPages_; return true;
}
void ResourceDomain::drop() {
    InterruptSpinLockGuard guard(domainLock);
    if (references_) --references_;
    Thread::wake(worker);
}
void ResourceDomain::closeLease() {
    InterruptSpinLockGuard guard(domainLock);
    if (state_ == State::Active) state_ = State::Pending;
    if (references_) --references_;
    Thread::wake(worker);
}
void ResourceDomain::ownerExiting(Task& owner) {
    InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains) if (domain.owner_ == &owner) {
        domain.exiting_ = true;
        if (domain.state_ == State::Active) domain.state_ = State::Pending;
    }
    Thread::wake(worker);
}
void ResourceDomain::addressSpaceReleased(Task& owner) {
    InterruptSpinLockGuard guard(domainLock);
    for (auto& domain : domains) if (domain.owner_ == &owner) domain.addressSpaceGone_ = true;
    Thread::wake(worker);
}
void ResourceDomain::unmap(uint64_t address, uint64_t size, bool& mapped) {
    PreemptionSpinLockGuard guard(owner_->addressSpace().lock());
    if (!__atomic_exchange_n(&mapped, false, __ATOMIC_ACQ_REL)) return;
    const uint64_t root = owner_->addressSpace().rootPhysicalAddress();
    if (root && owner_->addressSpace().generation() == addressGeneration_)
        for (uint64_t offset = 0; offset < size; offset += 4096) memory::unmapPageInAddressSpace(root, address + offset);
}
bool ResourceDomain::reset() {
    for (MmioRegion* region = mmio_; region; region = region->next_) unmap(region->virtualAddress_, region->size_, region->mapped_);
    uint32_t command = 0;
    if (!PciAccess::read(function_, 4, command) || !PciAccess::write(function_, 4, (command & 0xffff) & ~4U)) return false;
#ifdef OS_DEVICE_RESOURCE_TEST
    if (__atomic_load_n(&blockReset, __ATOMIC_ACQUIRE)) return false;
#endif
    *status_ = 0;
    asm volatile("mfence" ::: "memory");
    const uint64_t deadline = time::getMonotonicTime() + 100000000ULL;
    while (*status_ != 0) {
        if (time::getMonotonicTime() >= deadline) return false;
        schedule::yieldThread();
    }
    return PciAccess::read(function_, 4, command) && !(command & 4);
}
int ResourceDomain::stop(Task& owner) {
    {
        InterruptSpinLockGuard guard(domainLock);
        if (owner_ != &owner) return -3;
        if (state_ == State::Active) state_ = State::Pending;
        Thread::wake(worker);
    }
    const uint64_t deadline = time::getMonotonicTime() + 2000000000ULL;
    for (;;) {
        {
            InterruptSpinLockGuard guard(domainLock);
            if (state_ == State::Stopped) return 0;
            if (state_ == State::Quarantined) return -11;
        }
        if (time::getMonotonicTime() >= deadline) return -9;
        schedule::yieldThread();
    }
}
void ResourceDomain::work(void*) {
    for (;;) {
        ResourceDomain* pending = nullptr;
        {
            InterruptSpinLockGuard guard(domainLock);
            for (auto& domain : domains) {
                if (domain.state_ == State::Pending) { pending = &domain; domain.state_ = State::Stopping; break; }
                if (domain.state_ == State::Stopped && !domain.references_ && (!domain.exiting_ || domain.addressSpaceGone_)) { pending = &domain; break; }
            }
            if (!pending) Thread::prepareWait();
        }
        if (!pending) { Thread::blockCurrent(); continue; }
        if (pending->state_ == State::Stopping) {
            const bool stopped = pending->reset();
            {
                InterruptSpinLockGuard guard(domainLock);
                pending->state_ = stopped ? State::Stopped : State::Quarantined;
            }
            serial::serialWrite(stopped ? "[DEVICE] DMA domain stopped\n" : "[DEVICE] DMA domain quarantined; pages retained\n");
            continue;
        }
        // All handles are gone and all user mappings have been revoked or destroyed.
        Task* owner = pending->owner_;
        for (DmaBuffer* buffer = pending->dma_; buffer;) {
            DmaBuffer* next = buffer->next_;
            memory::freePhysicalPage(buffer->physicalAddress_);
            heap::kernelFree(buffer); buffer = next;
        }
        for (MmioRegion* region = pending->mmio_; region;) { MmioRegion* next = region->next_; heap::kernelFree(region); region = next; }
        memory::unmapPage(pending->controlMapping_);
        {
            InterruptSpinLockGuard guard(domainLock);
            *pending = ResourceDomain{};
        }
        owner->release();
        serial::serialWrite("[DEVICE] DMA domain reclaimed\n");
    }
}

void MmioRegion::destroy() {
    if (__atomic_exchange_n(&closed_, true, __ATOMIC_ACQ_REL)) return;
    domain_->unmap(virtualAddress_, size_, mapped_); domain_->drop();
}
void DmaBuffer::destroy() {
    if (__atomic_exchange_n(&closed_, true, __ATOMIC_ACQ_REL)) return;
    domain_->unmap(virtualAddress_, size_, mapped_); domain_->drop();
}
}  // namespace kernel::device
