#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/runtime.h"
#include "service/mm/mm.h"
namespace mm {
uint64_t ReclaimManager::reclaim(uint64_t bytes) {
    os::wire::MemoryUsage before{};
    if (os::detail::MemoryQuery().usage(before)) return 0;
    os::detail::MemoryControl().reclaimCaches();
    for (unsigned pass = 0; pass < 2; ++pass)
        for (auto& p : state_.pages) {
            if (!p.used || !p.mapped || p.io || p.pinned || p.cow || p.dirty)
                continue;
            const auto* backing = state_.backing(p.backing);
            if (!backing || backing->kind != BackingKind::File) continue;
            const uint64_t flags = KernelMemory::pageState(p.task, p.address);
            // Private dirty pages cannot be reconstructed from the source file.
            if ((flags & (1ULL << 6)) || (!pass && (flags & (1ULL << 5))))
                continue;
            if (os::detail::MemoryControl().unmapTarget(p.address, 4096,
                                                        p.task))
                continue;
            if (p.handle) os::closeHandle(p.handle);
            p = {};
            os::wire::MemoryUsage now{};
            if (!os::detail::MemoryQuery().usage(now) &&
                now.free >= before.free && now.free - before.free >= bytes)
                return now.free - before.free;
        }
    os::wire::MemoryUsage now{};
    return !os::detail::MemoryQuery().usage(now) && now.free > before.free
               ? now.free - before.free
               : 0;
}
}  // namespace mm
