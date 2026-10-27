/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/physical/buddy.cc
 * NUMA-aware buddy block splitting, coalescing, and page ownership.
 */

#include <new>

#include "kernel/page.h"

namespace kernel::mm {

// Initialize caller-provided metadata before publishing any free block. Every
// frame starts Reserved and points to itself; addRange later admits only
// firmware-approved RAM, never implicitly treating metadata as usable.
void BuddyAlloc::init(PageFrame* frames, uint32_t count) {
    frames_ = frames;
    count_  = count;
    free_   = 0;
    lock_.init();
    for (auto& node : heads_)
        for (auto& head : node) head = noFrame;
    for (uint32_t i = 0; i < count; ++i) {
        new (&frames_[i]) PageFrame{};
        frames_[i].head = i;
    }
}
// Caller holds lock_. Rewrite the whole block so each interior frame points to
// its head, then put order/state on that head only. This lets query map an
// interior address back to one owner/reference count after split or merge.
void BuddyAlloc::mark(uint32_t head, uint32_t order, uint16_t node,
                      FrameState state) {
    for (uint32_t i = 0; i < (1U << order); ++i) {
        auto& frame = frames_[head + i];
        frame.reset();
        frame.head  = head;
        frame.node  = node;
        frame.state = FrameState::Interior;
    }
    frames_[head].state = state;
    frames_[head].order = order;
}
// Link a Free head into the list for its exact node and order. Lists contain
// block heads only; interior frames must never become independent entries.
void BuddyAlloc::insert(uint32_t head) {
    auto& frame    = frames_[head];
    auto& first    = heads_[frame.node][frame.order];
    frame.previous = noFrame;
    frame.next     = first;
    if (first != noFrame) frames_[first].previous = head;
    first = head;
}
// Detach using both directions while the allocator lock is held. Clearing
// links prevents a later split/merge from accidentally unlinking it twice.
void BuddyAlloc::remove(uint32_t head) {
    auto& frame = frames_[head];
    if (frame.previous == noFrame)
        heads_[frame.node][frame.order] = frame.next;
    else
        frames_[frame.previous].next = frame.next;
    if (frame.next != noFrame) frames_[frame.next].previous = frame.previous;
    frame.next = frame.previous = noFrame;
}
// Accept a still-Reserved page interval on one NUMA node. Validate every frame
// before changing accounting, then partition into aligned power-of-two blocks;
// alignment and remaining length jointly limit each chosen order.
bool BuddyAlloc::addRange(uint32_t first, uint32_t count, uint16_t node) {
    if (!frames_ || !count || first >= count_ || count > count_ - first ||
        node >= maximumMemoryNodes)
        return false;
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    for (uint32_t i = first; i < first + count; ++i)
        if (frames_[i].state != FrameState::Reserved) return false;
    free_ += count;
    // Choose the largest block aligned at first that fits inside this range.
    // An unaligned block could later XOR to a buddy outside the admitted run.
    while (count) {
        uint32_t order = 0;
        while (order < maximumBuddyOrder &&
               (first & ((1U << (order + 1)) - 1)) == 0 &&
               (1U << (order + 1)) <= count)
            ++order;
        mark(first, order, node, FrameState::Free);
        insert(first);
        first += 1U << order;
        count -= 1U << order;
    }
    return true;
}
// Search the preferred node first, optionally stopping there for strict
// placement. order requests 2^order pages; limit is an exclusive physical end
// bound. Split a larger free block, return unused halves to their lists, then
// publish one allocated head with its initial owner/reference.
uint64_t BuddyAlloc::allocate(uint32_t order, uint64_t owner,
                              uint16_t preferred, bool strict, uint64_t limit) {
    if (!frames_ || order > maximumBuddyOrder ||
        (preferred != anyMemoryNode && preferred >= maximumMemoryNodes))
        return 0;
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    for (uint16_t pass = 0; pass < maximumMemoryNodes; ++pass) {
        const uint16_t node = preferred == anyMemoryNode
                                  ? pass
                                  : (preferred + pass) % maximumMemoryNodes;
        if (strict && preferred != anyMemoryNode && pass) break;
        for (uint32_t found = order; found <= maximumBuddyOrder; ++found) {
            uint32_t head = heads_[node][found];
            while (
                head != noFrame &&
                (head == 0 || (static_cast<uint64_t>(head) + (1ULL << order)) *
                                      physicalPageSize >
                                  limit))
                head = frames_[head].next;
            if (head == noFrame) continue;
            remove(head);
            // Keep the lower half as the candidate and return each upper half
            // as Free. Only the ultimately allocated pages reduce free_;
            // splitting itself does not consume RAM or create another owner.
            while (found > order) {
                --found;
                const uint32_t buddy = head + (1U << found);
                mark(buddy, found, node, FrameState::Free);
                insert(buddy);
            }
            mark(head, order, node, FrameState::Allocated);
            frames_[head].owner      = owner;
            frames_[head].references = 1;
            free_ -= 1ULL << order;
            return static_cast<uint64_t>(head) * physicalPageSize;
        }
    }
    return 0;
}
// Retain an allocated block head only; reject interior, stale and overflowing
// references. UINT64_MAX skips owner matching for trusted internal paths.
bool BuddyAlloc::retain(uint64_t address, uint64_t owner) {
    if ((address & 4095) || address / 4096 >= count_) return false;
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    auto& frame = frames_[address / 4096];
    if (frame.state != FrameState::Allocated || !frame.references ||
        frame.references == UINT32_MAX ||
        (owner != UINT64_MAX && frame.owner != owner))
        return false;
    ++frame.references;
    return true;
}
// Require matching head/order/owner before reducing references. A nonfinal
// release leaves the allocation untouched; only zero references permits
// coalescing and a return to free lists.
bool BuddyAlloc::release(uint64_t address, uint32_t order, uint64_t owner) {
    if ((address & 4095) || address / 4096 >= count_ ||
        order > maximumBuddyOrder)
        return false;
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    uint32_t head = address / 4096;
    auto& frame   = frames_[head];
    if (frame.state != FrameState::Allocated || frame.order != order ||
        !frame.references || (owner != UINT64_MAX && frame.owner != owner))
        return false;
    if (--frame.references) return true;
    const uint16_t node = frame.node;
    free_ += 1ULL << order;
    while (order < maximumBuddyOrder) {
        // XOR flips the bit selecting the sibling at this order. Merge only a
        // Free head of the same order and node; crossing a reserved hole, live
        // block or NUMA boundary would fabricate a larger contiguous
        // allocation.
        const uint32_t buddy = head ^ (1U << order);
        if (buddy >= count_ || frames_[buddy].state != FrameState::Free ||
            frames_[buddy].node != node || frames_[buddy].order != order)
            break;
        remove(buddy);
        // The merged block is represented by its lower address. Repeat at the
        // next order because this newly formed block may have another free
        // sibling.
        if (buddy < head) head = buddy;
        ++order;
    }
    mark(head, order, node, FrameState::Free);
    insert(head);
    return true;
}
// Assign an unowned allocation or reaffirm its owner. Do not silently move an
// already owned live block into an unrelated process accounting domain.
bool BuddyAlloc::assignOwner(uint64_t address, uint64_t owner) {
    if ((address & 4095) || address / 4096 >= count_) return false;
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    auto& frame = frames_[address / 4096];
    if (frame.state != FrameState::Allocated ||
        (frame.owner && frame.owner != owner))
        return false;
    frame.owner = owner;
    return true;
}
// Resolve any frame in a block to its head while locked. The returned wire
// info describes that block, not a separate reference per interior page.
bool BuddyAlloc::query(uint64_t address, PhysicalPageInfo& info) {
    if (address / 4096 >= count_) return false;
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    const auto& frame = frames_[frames_[address / 4096].head];
    info              = {frame.owner, frame.references, frame.node, frame.order,
                         frame.state};
    return true;
}
// Count allocated heads once, weighted by their order; scanning every frame as
// an independent page would double-count interior block metadata.
uint64_t BuddyAlloc::pagesOwned(uint64_t owner) {
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    uint64_t total = 0;
    for (uint32_t i = 0; i < count_; ++i)
        if (frames_[i].state == FrameState::Allocated &&
            frames_[i].owner == owner)
            total += 1ULL << frames_[i].order;
    return total;
}
// Free physical page count under the allocator lock.
uint64_t BuddyAlloc::freePages() {
    ::kernel::task::InterruptSpinLockGuard guard(lock_);
    return free_;
}

}  // namespace kernel::mm
