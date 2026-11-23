/*
 * src/mm/allocator.cc
 * 为补页申请单页对象，失败时回收后重试。
 */

#include <os/detail/client.h>
#include <os/runtime.h>

#include "service/mm/mm.h"

namespace mm {

    /*
     * 为目标任务申请一页内核页对象。
     * 首次失败时先回收可重建的缓存页，再重试一次；返回句柄或负错误码。
     */
    int64_t PageAllocator::allocate(uint64_t task) {
        // 补页器已经预留本次驻留记录，包括用于替换页面的记录，不能再次占用同一个名额。
        if (state_.pagesUsed(task) > MemoryState::taskPageLimit) {
            return static_cast<int64_t>(os::ErrorCode::OutOfMemory);
        }

        int64_t handle =
            os::detail::MemoryControl().allocatePages(0, UINT16_MAX, task);

        if (handle > 0) {
            return handle;
        }

        // 首次申请失败才回收，并只重试一次，避免内存耗尽时在补页服务里无限循环。
        reclaim_.reclaim(4096);
        handle = os::detail::MemoryControl().allocatePages(0, UINT16_MAX, task);

        return handle ? handle
                      : static_cast<int64_t>(os::ErrorCode::OutOfMemory);
    }

}  // namespace mm
