#include <new>

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"

namespace kernel::device {

DmaBuffer* DmaBuffer::create(::kernel::task::Task& task, ResourceDomain& domain,
                             uint64_t userAddress,
                             os::wire::DmaBufferInfo& info, uint64_t size,
                             uint64_t alignment) {
    if (!size || size > 8 * 1024 * 1024ULL || !alignment ||
        (alignment & (alignment - 1)) || alignment > 8 * 1024 * 1024ULL ||
        !domain.active(task))
        return nullptr;
    uint32_t order = 0;
    while ((4096ULL << order) < size || (4096ULL << order) < alignment) ++order;
    const uint64_t bytes = 4096ULL << order;
    if (!userAddress) userAddress = reserveDisplayAddress(bytes);
    if ((userAddress & 4095) || userAddress < 0x10000 ||
        userAddress > 0x800000000000ULL - bytes)
        return nullptr;
    ::kernel::task::PreemptionSpinLockGuard guard(task.addressSpace().lock());
    const uint64_t root = task.addressSpace().rootPhysicalAddress();
    if (!root) return nullptr;
    const uint64_t physical = mm::allocatePhysicalBlock(order, task.id());
    if (!physical) return nullptr;
    uint64_t mapped = 0;
    for (; mapped < bytes; mapped += 4096)
        if (!mm::mapPageInAddressSpace(
                root, userAddress + mapped, physical + mapped,
                ::kernel::mm::pageUser | ::kernel::mm::pageWritable |
                    ::kernel::mm::pageNoExecute | ::kernel::mm::pageExternal))
            break;
    if (mapped != bytes) {
        while (mapped) {
            mapped -= 4096;
            mm::unmapPageInAddressSpace(root, userAddress + mapped);
        }
        mm::releasePhysicalBlock(physical, order, task.id());
        return nullptr;
    }
    void* storage = mm::allocateHeap(sizeof(DmaBuffer));
    auto* buffer  = storage ? new (storage) DmaBuffer : nullptr;
    if (!buffer) {
        for (uint64_t i = 0; i < bytes; i += 4096)
            mm::unmapPageInAddressSpace(root, userAddress + i);
        mm::releasePhysicalBlock(physical, order, task.id());
        return nullptr;
    }
    buffer->physicalAddress_ = physical;
    buffer->virtualAddress_  = userAddress;
    buffer->size_            = bytes;
    buffer->mapped_          = true;
    mm::clearPhysicalPages(physical, bytes);
    if (!domain.attach(*buffer)) {
        for (uint64_t i = 0; i < bytes; i += 4096)
            mm::unmapPageInAddressSpace(root, userAddress + i);
        mm::releasePhysicalBlock(physical, order, task.id());
        mm::freeHeap(buffer);
        return nullptr;
    }
    info.virtualAddress = userAddress;
    info.deviceAddress  = physical;
    info.size           = bytes;
    return buffer;
}

void DmaBuffer::destroy() {
    if (__atomic_exchange_n(&closed_, true, __ATOMIC_ACQ_REL)) return;
    domain_->unmap(virtualAddress_, size_, mapped_, physicalAddress_);
    domain_->drop();
}

}  // namespace kernel::device
