#include "mm.h"
#include "os/file.h"
#include "os/mm.h"
#include "os/task.h"
namespace mm {
ResidentPage* MemoryState::page(uint64_t task, uint64_t address) {
    for (auto& p : pages) if (p.used && p.task == task && p.address == address) return &p;
    return nullptr;
}
ResidentPage* MemoryState::vacantPage() { for (auto& p : pages) if (!p.used) return &p; return nullptr; }
Backing* MemoryState::backing(uint64_t id) { for (auto& b : backings) if (b.used && b.id == id) return &b; return nullptr; }
Backing* MemoryState::createBacking(BackingKind kind) {
    for (auto& b : backings) if (!b.used) { b = {}; b.used = true; b.kind = kind; b.id = nextBacking_++; return &b; }
    return nullptr;
}
uint64_t MemoryState::pagesUsed(uint64_t task) const { uint64_t count = 0; for (const auto& p : pages) if (p.used && p.task == task) ++count; return count; }
void MemoryState::collectBackings() {
    for (auto& b : backings) {
        if (!b.used || regions.references(b.id)) continue;
        bool live = false; for (const auto& p : pages) if (p.used && p.backing == b.id) live = true;
        if (live) continue;
        if (b.handle) os::closeHandle(b.handle);
        if (b.descriptor) os::File::borrow(b.fs, b.descriptor).close();
        if (b.fs) os::closeHandle(b.fs);
        b = {};
    }
}
void MemoryState::releaseTask(uint64_t task) {
    for (auto& domain : domains_) if (domain.task == task) domain = {};
    regions.removeTask(task);
    for (auto& p : pages) if (p.used && p.task == task) {
        if (p.handle) os::closeHandle(p.handle);
        p = {};
    }
    collectBackings();
}
}  // namespace mm

namespace mm {
int MemoryState::synchronize(uint64_t task) {
    os::MemoryUsage usage{}; if (os::Memory::usage(usage, task)) { releaseTask(task); return -4; }
    for (auto& domain : domains_) if (domain.task == task) {
        if (domain.generation == usage.generation) return 0;
        releaseTask(task); domain = {task, usage.generation}; return 1;
    }
    for (auto& domain : domains_) if (!domain.task) { domain = {task, usage.generation}; return 0; }
    return -5;
}
void MemoryState::sweep() {
    for (auto& domain : domains_) if (domain.task) {
        os::TaskInfo info{};
        if (os::Task::query(domain.task, info) || info.state == 2) { releaseTask(domain.task); continue; }
        os::MemoryUsage usage{};
        if (os::Memory::usage(usage, domain.task) || usage.generation != domain.generation) releaseTask(domain.task);
    }
}
}  // namespace mm
