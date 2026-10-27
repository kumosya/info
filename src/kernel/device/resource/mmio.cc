#include <new>

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"

namespace kernel::device {

uint64_t reserveDisplayAddress(uint64_t size) {
    static uint64_t next = 0x0000020000000000ULL;
    if (!size || size > UINT64_MAX - 8191) return 0;
    const uint64_t bytes = ((size + 4095) & ~4095ULL) + 4096;
    uint64_t start       = __atomic_load_n(&next, __ATOMIC_RELAXED);
    for (;;) {
        if (start > 0x0000700000000000ULL - bytes) return 0;
        const uint64_t end = start + bytes;
        if (__atomic_compare_exchange_n(&next, &start, end, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return start;
    }
}

MmioRegion* MmioRegion::map(::kernel::task::Task& task,
                            const os::wire::PciFunctionInfo& function,
                            uint8_t barIndex, uint64_t userAddress,
                            uint64_t size, uint64_t offset) {
    if (!userAddress) userAddress = reserveDisplayAddress(size);
    if (barIndex >= 6 || (userAddress & (mm::physicalPageSize - 1)) != 0 ||
        userAddress < 0x10000 || size == 0 ||
        userAddress + size < userAddress ||
        userAddress + size >= 0x0000800000000000ULL ||
        (size & (mm::physicalPageSize - 1)) != 0) {
        return nullptr;
    }
    ResourceDomain* domain = resourceDomains().find(task, function);
    if (!domain) return nullptr;
    auto create = [&]() -> MmioRegion* {
        const uint64_t barBase = domain->barBases_[barIndex],
                       barSize = domain->barSizes_[barIndex];
        if (!barBase || offset > barSize || size > barSize - offset ||
            (offset & 4095) || (barBase & (mm::physicalPageSize - 1)))
            return nullptr;
        ::kernel::task::PreemptionSpinLockGuard guard(
            task.addressSpace().lock());
        if (!task.addressSpace().rootPhysicalAddress()) return nullptr;
        for (uint64_t pageOffset = 0; pageOffset < size;
             pageOffset += mm::physicalPageSize) {
            if (!mm::mapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    userAddress + pageOffset, barBase + offset + pageOffset,
                    ::kernel::mm::pageUser | ::kernel::mm::pageWritable |
                        ::kernel::mm::pageNoExecute |
                        ::kernel::mm::pageCacheDisable |
                        ::kernel::mm::pageExternal)) {
                while (pageOffset != 0) {
                    pageOffset -= mm::physicalPageSize;
                    mm::unmapPageInAddressSpace(
                        task.addressSpace().rootPhysicalAddress(),
                        userAddress + pageOffset);
                }
                return nullptr;
            }
        }
        void* storage = mm::allocateHeap(sizeof(MmioRegion));
        MmioRegion* region =
            storage == nullptr ? nullptr : new (storage) MmioRegion;
        if (region == nullptr) {
            for (uint64_t pageOffset = 0; pageOffset < size;
                 pageOffset += mm::physicalPageSize)
                mm::unmapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    userAddress + pageOffset);
            return nullptr;
        }
        region->physicalAddress_ = barBase + offset;
        region->virtualAddress_  = userAddress;
        region->size_            = size;
        region->mapped_          = true;
        if (!domain->attach(*region)) {
            for (uint64_t pageOffset = 0; pageOffset < size;
                 pageOffset += mm::physicalPageSize)
                mm::unmapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    userAddress + pageOffset);
            mm::freeHeap(region);
            return nullptr;
        }
        return region;
    };
    MmioRegion* region = create();
    domain->drop();
    return region;
}

uint64_t MmioRegion::address() const { return virtualAddress_; }

uint64_t MmioRegion::size() const { return size_; }

void MmioRegion::destroy() {
    if (__atomic_exchange_n(&closed_, true, __ATOMIC_ACQ_REL)) return;
    domain_->unmap(virtualAddress_, size_, mapped_, physicalAddress_);
    domain_->drop();
}

}  // namespace kernel::device
