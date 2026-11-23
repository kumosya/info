/*
 * src/mm/reclaim.cc
 * 回收内核空闲缓存及可从文件重新读入的干净页面。
 */

#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/runtime.h"
#include "service/mm/mm.h"

namespace mm {

    /*
     * 先回收内核空闲缓存，再选择可从文件重新读取的干净页面。
     * 跳过固定、正在读写、脏页及写时复制页，按实际空闲量变化统计回收结果。
     */
    uint64_t ReclaimManager::reclaim(uint64_t bytes) {
        os::wire::MemoryUsage before{};

        if (os::detail::MemoryQuery().usage(before)) {
            return 0;
        }

        os::detail::MemoryControl().reclaimCaches();

        // 第一轮避开近期访问页，第二轮再放宽这一条件；两轮都只选能够从文件重新构造的页面。
        for (unsigned pass = 0; pass < 2; ++pass) {
            for (ResidentPage& p : state_.pages) {
                // 固定、正在读写、写时复制或已经脏的页面都不能在这里直接丢弃。
                if (!p.used || !p.mapped || p.io || p.pinned || p.cow ||
                    p.dirty) {
                    continue;
                }

                const Backing* backing = state_.backing(p.backing);

                if (!backing || backing->kind != BackingKind::File) {
                    continue;
                }

                const uint64_t flags =
                    KernelMemory::pageState(p.task, p.address);
                // 私有脏页已不同于原文件，不能直接丢弃后声称可以从源文件重建。
                if ((flags & ((1ULL << 6) | (1ULL << 10))) ||
                    (!pass && (flags & (1ULL << 5)))) {
                    continue;
                }

                if (os::detail::MemoryControl().unmapTarget(p.address, 4096,
                                                            p.task)) {
                    continue;
                }

                // 映射撤销成功以后再关闭页对象句柄，最后用实际空闲量变化判断是否达到回收目标。
                if (p.handle) {
                    os::closeHandle(p.handle);
                }

                p = {};
                os::wire::MemoryUsage now{};

                if (!os::detail::MemoryQuery().usage(now) &&
                    now.free >= before.free &&
                    now.free - before.free >= bytes) {
                    return now.free - before.free;
                }
            }
        }

        os::wire::MemoryUsage now{};

        return !os::detail::MemoryQuery().usage(now) && now.free > before.free
                   ? now.free - before.free
                   : 0;
    }

}  // namespace mm
