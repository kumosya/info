#include "kernel/page.h"
#include "kernel/mm.h"
namespace kernel::memory {
PageObject::PageObject(uint32_t order, uint64_t owner, uint16_t node)
    : owner_(owner), order_(order) {
    if (order > 12) return;
    physical_ = allocatePhysicalBlock(order, owner, node);
    if (!physical_) return;
    KernelMapping::clear(physical_, size());
}
PageObject::~PageObject() {
    if (physical_) releasePhysicalBlock(physical_, order_, owner_);
}
void PageObject::retain() {
    // Every mapping owns a block reference as well as an object reference.
    retainPhysicalBlock(physical_, owner_);
    __atomic_add_fetch(&references_, 1U, __ATOMIC_RELAXED);
}
void PageObject::release() {
    const bool last = __atomic_sub_fetch(&references_, 1U, __ATOMIC_ACQ_REL) == 0;
    if (last) {
        this->~PageObject();
        heap::kernelFree(this);
    } else if (physical_) releasePhysicalBlock(physical_, order_, owner_);
}
}  // namespace kernel::memory
