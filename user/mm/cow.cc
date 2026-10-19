#include "mm.h"
#include "os/task.h"
namespace mm {
int Pager::clone(uint64_t parent, uint64_t child) {
    os::TaskInfo info{};
    if (os::Process::queryTask(child, info) || info.parentId != parent || info.state != 0 || child == parent) return -3;
    if (state_.synchronize(child) < 0) return -5;
    RegionTable staged = state_.regions;
    for (size_t i = 0; i < RegionTable::capacity; ++i) {
        auto region = state_.regions.at(i);
        if (!region.used || region.task != parent) continue;
        region.task = child;
        if (!staged.insert(region)) return -5;
    }
    size_t required = 0, available = 0;
    for (const auto& page : state_.pages) {
        if (!page.used) ++available;
        else if (page.task == parent) ++required;
    }
    if (available < required) return -5;
    for (auto& source : state_.pages) {
        if (!source.used || source.task != parent) continue;
        const auto* region = state_.regions.find(parent, source.address);
        if (!region) continue;
        auto* copy = state_.vacantPage();
        *copy = source; copy->task = child;
        if (source.handle) {
            const int64_t handle = os::Handle::duplicate(source.handle, 7);
            if (handle <= 0) { *copy = {}; state_.releaseTask(child); return -5; }
            copy->handle = handle;
        }
        if (!region->shared && source.handle) {
            source.dirty = source.dirty || (KernelMemory::pageState(parent, source.address) & (1ULL << 6));
            copy->dirty = source.dirty; copy->cow = source.cow = true;
            if ((source.mapped && os::Memory::protectTarget(source.address, 4096, 4, parent)) ||
                (copy->mapped && os::Memory::protectTarget(copy->address, 4096, 4, child))) {
                state_.releaseTask(child); return -11;
            }
        }
    }
    state_.regions = staged;
    return 0;
}

int Pager::copyPrivate(ResidentPage& page, const Region& region) {
    constexpr uint64_t source = 0x60000000, destination = source + 4096;
    const int64_t handle = allocator_.allocate(page.task);
    if (handle <= 0) return handle;
    if (os::Memory::mapPages(page.handle, source, 4096, 0, 0) != source) { os::closeHandle(handle); return -5; }
    if (os::Memory::mapPages(handle, destination, 4096, 0, 1) != destination) {
        os::Memory::unmapTarget(source, 4096); os::closeHandle(handle); return -5;
    }
    os::Runtime::copyBytes(reinterpret_cast<void*>(destination), reinterpret_cast<void*>(source), 4096);
    os::Memory::unmapTarget(destination, 4096); os::Memory::unmapTarget(source, 4096);
    const uint64_t protection = KernelMemory::nativeProtection(region.protection);
    if (page.mapped && os::Memory::unmapTarget(page.address, 4096, page.task)) { os::closeHandle(handle); return -11; }
    page.mapped = false;
    if (os::Memory::mapPages(handle, page.address, 4096, 0, protection, page.task) != page.address) {
        if (os::Memory::mapPages(page.handle, page.address, 4096, 0, protection & ~1ULL, page.task) == page.address) {
            page.mapped = true; os::Memory::protectTarget(page.address, 4096, 4, page.task);
        }
        os::closeHandle(handle); return -5;
    }
    os::closeHandle(page.handle); page.handle = handle;
    page.cow = false; page.dirty = true; page.mapped = true; page.lastUse = ++state_.clock;
    return 0;
}
}  // namespace mm
