#include "service/common/runtime.h"
#include "service/common/queries.h"
#include <os/file.h>
#include "service/mm/mm.h"
namespace mm {
int Pager::loadFile(ResidentPage& page, const Region& region,
                    Backing& backing) {
    constexpr uint64_t scratch = 0x60000000;
    if (!backing.descriptor || !backing.fs) return -11;
    if (static_cast<int64_t>(os::detail::MemoryControl().mapPages(
            page.handle, scratch, 4096, 0, 1)) != static_cast<int64_t>(scratch))
        return -5;
    const uint64_t offset = region.offset + page.address - region.address;
    os::File file;
    if (file.borrow(backing.fs, backing.descriptor, nullptr, O_RDONLY)) {
        os::detail::MemoryControl().unmapTarget(scratch, 4096);
        return -11;
    }
    if (file.refresh() || offset >= file.getSize()) {
        os::detail::MemoryControl().unmapTarget(scratch, 4096);
        return -18;
    }
    page.io = true;
    const int64_t read =
        file.pread(reinterpret_cast<void*>(scratch), 4096, offset);
    page.io            = false;
    const int unmapped = os::detail::MemoryControl().unmapTarget(scratch, 4096);
    page.fileOffset    = offset;
    return read < 0 || unmapped ? -11 : 0;
}
int Pager::fault(uint64_t task, uint64_t address, uint64_t flags) {
    const Region* region = state_.regions.find(task, address);
    if (!region) return -4;
    if ((flags & 16)  ? !(region->protection & 4)
        : (flags & 2) ? !(region->protection & 2)
                      : !(region->protection & 1))
        return -3;
    address &= ~4095ULL;
    const uint64_t protection =
        KernelMemory::nativeProtection(region->protection);
    ResidentPage* page = state_.page(task, address);
    if (page && page->cow && (flags & 2)) return copyPrivate(*page, *region);
    if (page && page->mapped) {
        const uint64_t actual = KernelMemory::pageState(task, address);
        if ((actual & 1) && (!(flags & 2) || (actual & 2)) &&
            (!(flags & 16) || !(actual & (1ULL << 63)))) {
            page->lastUse = ++state_.clock;
            return 0;
        }
        if (actual & 1) return -3;
        page->mapped = false;
    }
    bool fresh = page == nullptr;
    if (fresh) {
        if (state_.pagesUsed(task) >= 2048) return -5;
        page = state_.vacantPage();
        if (!page) {
            reclaim_.reclaim(4096);
            page = state_.vacantPage();
        }
        if (!page) return -5;
        *page         = {};
        page->used    = true;
        page->task    = task;
        page->address = address;
        page->backing = region->backing;
    }
    Backing* backing = state_.backing(region->backing);
    if (!backing) {
        if (fresh) *page = {};
        return -2;
    }
    if (backing->kind == BackingKind::Shared) {
        const int result = KernelMemory::mapShared(
            backing->handle, task, address, 4096,
            region->offset + address - region->address, protection);
        if (result) {
            if (fresh) *page = {};
            return result;
        }
    } else {
        if (!page->handle) {
            const int64_t handle = allocator_.allocate(task);
            if (handle <= 0) {
                if (fresh) *page = {};
                return handle < 0 ? handle : -5;
            }
            page->handle = handle;
            if (backing->kind == BackingKind::File) {
                const int result = loadFile(*page, *region, *backing);
                if (result) {
                    os::closeHandle(page->handle);
                    *page = {};
                    return result;
                }
            }
        }
        if (os::detail::MemoryControl().mapPages(
                page->handle, address, 4096, 0,
                page->cow ? protection & ~1ULL : protection, task) != address) {
            if (fresh) {
                os::closeHandle(page->handle);
                *page = {};
            }
            return -5;
        }
        if (page->cow &&
            os::detail::MemoryControl().protectTarget(address, 4096, 4, task))
            return -11;
    }
    page->mapped  = true;
    page->lastUse = ++state_.clock;
    return 0;
}
}  // namespace mm
