#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/mm/mm.h"
#include <os/file.h>
#include <os/mm.h>
#include <os/task.h>
namespace mm {
ResidentPage* MemoryState::page(uint64_t task, uint64_t address) {
    for (auto& p : pages)
        if (p.used && p.task == task && p.address == address) return &p;
    return nullptr;
}
ResidentPage* MemoryState::vacantPage() {
    for (auto& p : pages)
        if (!p.used) return &p;
    return nullptr;
}
Backing* MemoryState::backing(uint64_t id) {
    for (auto& b : backings)
        if (b.used && b.id == id) return &b;
    return nullptr;
}
Backing* MemoryState::createBacking(BackingKind kind) {
    for (auto& b : backings)
        if (!b.used) {
            b      = {};
            b.used = true;
            b.kind = kind;
            b.id   = nextBacking_++;
            return &b;
        }
    return nullptr;
}
uint64_t MemoryState::pagesUsed(uint64_t task) const {
    uint64_t count = 0;
    for (const auto& p : pages)
        if (p.used && p.task == task) ++count;
    return count;
}
bool MemoryState::references(uint64_t id) const {
    if (regions.references(id)) return true;
    for (const auto& page : pages)
        if (page.used && page.backing == id) return true;
    return false;
}
void MemoryState::collectBackings(bool releaseFiles) {
    for (auto& b : backings) {
        if (!b.used || regions.references(b.id)) continue;
        bool live = false;
        for (const auto& p : pages)
            if (p.used && p.backing == b.id) live = true;
        if (live) continue;
        if (b.lease) {
            if (!releaseFiles) continue;
            os::IpcMessage request{}, reply{};
            request.type = static_cast<uint64_t>(os::FsRequest::ReleaseMapping);
            request.length  = 1;
            request.data[0] = b.lease;
            if (!b.fs || os::callService(b.fs, request, reply) ||
                static_cast<int64_t>(reply.data[0]))
                continue;
        }
        if (b.descriptor) {
            os::File file;
            if (file.adopt(b.fs, b.descriptor, nullptr, O_RDONLY) ||
                file.close())
                continue;
            b.descriptor = 0;
        }
        if (b.handle) os::closeHandle(b.handle);
        if (b.fs) os::closeHandle(b.fs);
        b = {};
    }
}
void MemoryState::releaseTask(uint64_t task) {
    for (auto& domain : domains_)
        if (domain.task == task) domain = {};
    regions.removeTask(task);
    for (auto& p : pages)
        if (p.used && p.task == task) {
            if (p.handle) os::closeHandle(p.handle);
            p = {};
        }
    collectBackings();
}
}  // namespace mm

namespace mm {
int MemoryState::synchronize(uint64_t task) {
    os::wire::MemoryUsage usage{};
    if (os::detail::MemoryQuery().usage(usage, task)) {
        releaseTask(task);
        return -4;
    }
    for (auto& domain : domains_)
        if (domain.task == task) {
            if (domain.generation == usage.generation) return 0;
            releaseTask(task);
            domain = {task, usage.generation};
            return 1;
        }
    for (auto& domain : domains_)
        if (!domain.task) {
            domain = {task, usage.generation};
            return 0;
        }
    return -5;
}
bool MemoryState::sweep() {
    bool changed = false;
    for (auto& domain : domains_)
        if (domain.task) {
            os::wire::TaskInfo info{};
            if (os::detail::TaskQuery().query(domain.task, info) ||
                info.state == 2) {
                releaseTask(domain.task);
                changed = true;
                continue;
            }
            os::wire::MemoryUsage usage{};
            if (os::detail::MemoryQuery().usage(usage, domain.task) ||
                usage.generation != domain.generation) {
                releaseTask(domain.task);
                changed = true;
            }
        }
    return changed;
}
void MemoryState::pruneRecovered() {
    for (auto& domain : domains_)
        if (domain.task) {
            os::wire::MemoryUsage usage{};
            if (!os::detail::MemoryQuery().usage(usage, domain.task) &&
                usage.generation == domain.generation)
                continue;
            regions.removeTask(domain.task);
            for (auto& page : pages)
                if (page.used && page.task == domain.task) page = {};
            domain = {};
        }
    for (auto& backing : backings)
        if (backing.used && !references(backing.id)) backing = {};
}
}  // namespace mm
