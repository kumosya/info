#include "mm.h"
namespace mm {
int64_t PageAllocator::allocate(uint64_t task) {
    // The pager has already reserved this resident record, including replacements.
    if (state_.pagesUsed(task) > 2048) return static_cast<int64_t>(os::ErrorCode::OutOfMemory);
    int64_t handle = os::Memory::allocatePages(0, UINT16_MAX, task);
    if (handle > 0) return handle;
    reclaim_.reclaim(4096);
    handle = os::Memory::allocatePages(0, UINT16_MAX, task);
    return handle ? handle : static_cast<int64_t>(os::ErrorCode::OutOfMemory);
}
}  // namespace mm
