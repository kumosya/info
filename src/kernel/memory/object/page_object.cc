/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/page_object.cc
 * Reference-counted ownership of contiguous physical page blocks.
 */

#include "kernel/mm.h"
#include "kernel/page.h"

namespace kernel::mm {

// Allocate 2^order owner-tagged pages, then zero them before any client can
// map the object. Construction can leave physical_ zero; callers check valid
// instead of assuming object-storage allocation also obtained page backing.
PageObject::PageObject(uint32_t order, uint64_t owner, uint16_t node)
    : owner_(owner), order_(order) {
    if (order > 12) return;
    physical_ = allocatePhysicalBlock(order, owner, node);
    if (!physical_) return;
    clearPhysicalPages(physical_, size());
}
// Release the final physical-block reference using the original owner/order.
PageObject::~PageObject() {
    if (physical_) releasePhysicalBlock(physical_, order_, owner_);
}
// Object references and physical-block references advance together. A map can
// therefore outlive a handle without losing its backing allocation.
void PageObject::retain() {
    // Every mapping owns a block reference as well as an object reference.
    retainPhysicalBlock(physical_, owner_);
    __atomic_add_fetch(&references_, 1U, __ATOMIC_RELAXED);
}
// Each nonfinal object release drops its paired block reference. The last
// release runs the destructor for that final block reference, then frees
// wrapper storage; calling both paths would decrement the block twice.
void PageObject::release() {
    const bool last =
        __atomic_sub_fetch(&references_, 1U, __ATOMIC_ACQ_REL) == 0;
    if (last) {
        this->~PageObject();
        mm::freeHeap(this);
    } else if (physical_)
        releasePhysicalBlock(physical_, order_, owner_);
}

}  // namespace kernel::mm
