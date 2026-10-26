#include "kernel/page.h"

namespace kernel::memory {
void BuddyAllocator::init(PageFrame* frames, uint32_t count) {
    frames_ = frames; count_ = count; free_ = 0; lock_.init();
    for (auto& node : heads_) for (auto& head : node) head = noFrame;
    for (uint32_t i = 0; i < count; ++i) {
        frames_[i] = {}; frames_[i].head = i;
    }
}
void BuddyAllocator::mark(uint32_t head, uint32_t order, uint16_t node, FrameState state) {
    for (uint32_t i = 0; i < (1U << order); ++i) {
        auto& frame = frames_[head + i]; frame = {};
        frame.head = head; frame.node = node; frame.state = FrameState::Interior;
    }
    frames_[head].state = state; frames_[head].order = order;
}
void BuddyAllocator::insert(uint32_t head) {
    auto& frame = frames_[head]; auto& first = heads_[frame.node][frame.order];
    frame.previous = noFrame; frame.next = first;
    if (first != noFrame) frames_[first].previous = head;
    first = head;
}
void BuddyAllocator::remove(uint32_t head) {
    auto& frame = frames_[head];
    if (frame.previous == noFrame) heads_[frame.node][frame.order] = frame.next;
    else frames_[frame.previous].next = frame.next;
    if (frame.next != noFrame) frames_[frame.next].previous = frame.previous;
    frame.next = frame.previous = noFrame;
}
bool BuddyAllocator::addRange(uint32_t first, uint32_t count, uint16_t node) {
    if (!frames_ || !count || first >= count_ || count > count_ - first || node >= maximumMemoryNodes) return false;
    InterruptSpinLockGuard guard(lock_);
    for (uint32_t i = first; i < first + count; ++i) if (frames_[i].state != FrameState::Reserved) return false;
    free_ += count;
    while (count) {
        uint32_t order = 0;
        while (order < maximumBuddyOrder && (first & ((1U << (order + 1)) - 1)) == 0 && (1U << (order + 1)) <= count) ++order;
        mark(first, order, node, FrameState::Free); insert(first);
        first += 1U << order; count -= 1U << order;
    }
    return true;
}
uint64_t BuddyAllocator::allocate(uint32_t order, uint64_t owner, uint16_t preferred, bool strict, uint64_t limit) {
    if (!frames_ || order > maximumBuddyOrder || (preferred != anyMemoryNode && preferred >= maximumMemoryNodes)) return 0;
    InterruptSpinLockGuard guard(lock_);
    for (uint16_t pass = 0; pass < maximumMemoryNodes; ++pass) {
        const uint16_t node = preferred == anyMemoryNode ? pass : (preferred + pass) % maximumMemoryNodes;
        if (strict && preferred != anyMemoryNode && pass) break;
        for (uint32_t found = order; found <= maximumBuddyOrder; ++found) {
            uint32_t head = heads_[node][found];
            while (head != noFrame && (head == 0 || (static_cast<uint64_t>(head) + (1ULL << order)) * physicalPageSize > limit)) head = frames_[head].next;
            if (head == noFrame) continue;
            remove(head);
            while (found > order) {
                --found; const uint32_t buddy = head + (1U << found);
                mark(buddy, found, node, FrameState::Free); insert(buddy);
            }
            mark(head, order, node, FrameState::Allocated);
            frames_[head].owner = owner; frames_[head].references = 1;
            free_ -= 1ULL << order;
            return static_cast<uint64_t>(head) * physicalPageSize;
        }
    }
    return 0;
}
bool BuddyAllocator::retain(uint64_t address, uint64_t owner) {
    if ((address & 4095) || address / 4096 >= count_) return false;
    InterruptSpinLockGuard guard(lock_); auto& frame = frames_[address / 4096];
    if (frame.state != FrameState::Allocated || !frame.references || frame.references == UINT32_MAX ||
        (owner != UINT64_MAX && frame.owner != owner)) return false;
    ++frame.references; return true;
}
bool BuddyAllocator::release(uint64_t address, uint32_t order, uint64_t owner) {
    if ((address & 4095) || address / 4096 >= count_ || order > maximumBuddyOrder) return false;
    InterruptSpinLockGuard guard(lock_); uint32_t head = address / 4096;
    auto& frame = frames_[head];
    if (frame.state != FrameState::Allocated || frame.order != order || !frame.references ||
        (owner != UINT64_MAX && frame.owner != owner)) return false;
    if (--frame.references) return true;
    const uint16_t node = frame.node; free_ += 1ULL << order;
    while (order < maximumBuddyOrder) {
        const uint32_t buddy = head ^ (1U << order);
        if (buddy >= count_ || frames_[buddy].state != FrameState::Free || frames_[buddy].node != node || frames_[buddy].order != order) break;
        remove(buddy); if (buddy < head) head = buddy; ++order;
    }
    mark(head, order, node, FrameState::Free); insert(head); return true;
}
bool BuddyAllocator::assignOwner(uint64_t address, uint64_t owner) {
    if ((address & 4095) || address / 4096 >= count_) return false;
    InterruptSpinLockGuard guard(lock_); auto& frame = frames_[address / 4096];
    if (frame.state != FrameState::Allocated || (frame.owner && frame.owner != owner)) return false;
    frame.owner = owner; return true;
}
bool BuddyAllocator::query(uint64_t address, PhysicalPageInfo& info) {
    if (address / 4096 >= count_) return false;
    InterruptSpinLockGuard guard(lock_);
    const auto& frame = frames_[frames_[address / 4096].head];
    info = {frame.owner, frame.references, frame.node, frame.order, frame.state}; return true;
}
uint64_t BuddyAllocator::pagesOwned(uint64_t owner) {
    InterruptSpinLockGuard guard(lock_); uint64_t total = 0;
    for (uint32_t i = 0; i < count_; ++i) if (frames_[i].state == FrameState::Allocated && frames_[i].owner == owner) total += 1ULL << frames_[i].order;
    return total;
}
uint64_t BuddyAllocator::freePages() { InterruptSpinLockGuard guard(lock_); return free_; }
}  // namespace kernel::memory
