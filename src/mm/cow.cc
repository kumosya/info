/*
 * src/mm/cow.cc
 * 复制 fork 的区域和页记录，并在首次写入时生成私有页面。
 */

#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/mm/mm.h"
#include <os/task.h>

namespace mm {

    /*
     * 为 fork 复制父进程的区域和驻留页记录。
     * 私有页面先共享同一页对象并去掉写权限，直到某一方写入时再复制；中途失败清理子任务记录。
     */
    int Pager::clone(uint64_t parent, uint64_t child) {
        os::wire::TaskInfo info{};

        // 子任务必须确实属于本次父任务且尚未运行，不能给正在执行的任意任务覆盖内存管理记录。
        if (os::detail::TaskQuery().query(child, info) ||
            info.parentId != parent || info.state != 0 || child == parent) {
            return -3;
        }

        if (state_.synchronize(child) < 0) {
            return -5;
        }

        // 先复制区域表作为草稿；全部区域插入成功前，不改变正式区域表。
        RegionTable staged = state_.regions;

        for (size_t i = 0; i < RegionTable::capacity; ++i) {
            Region region = state_.regions.at(i);

            if (!region.used || region.task != parent) {
                continue;
            }

            region.task = child;

            if (!staged.insert(region)) {
                return -5;
            }
        }

        // 先计算父页面需要的记录数，避免复制到一半才发现驻留表没有足够槽位。
        size_t required = 0, available = 0;

        for (const ResidentPage& page : state_.pages) {
            if (!page.used) {
                ++available;
            } else if (page.task == parent) {
                ++required;
            }
        }

        if (available < required) {
            return -5;
        }

        for (ResidentPage& source : state_.pages) {
            if (!source.used || source.task != parent) {
                continue;
            }

            const Region* region = state_.regions.find(parent, source.address);

            if (!region) {
                continue;
            }

            ResidentPage* copy = state_.vacantPage();
            *copy              = source;
            copy->task         = child;

            // 孩子不继承父进程的锁页状态；它自己的锁页额度和后续锁定请求分别管理。
            copy->pinned = false;

            // 子记录单独持有复制句柄，父进程关闭或退出后也不会让孩子失去共享页面。
            if (source.handle) {
                os::Ipc::Handle handleOwner;
                const int handleStatus = os::Ipc::Handle(source.handle, false)
                                             .duplicate(7, 0, handleOwner);
                const int64_t handle =
                    handleStatus ? handleStatus
                                 : static_cast<int64_t>(handleOwner.release());

                if (handle <= 0) {
                    *copy = {};
                    state_.releaseTask(child);

                    return -5;
                }

                copy->handle = handle;
            }

            if (!region->shared && source.handle) {
                // 先把硬件脏位并入服务记录，再把父子页都改成写时复制，原内容已被修改的信息不能丢失。
                source.dirty = source.dirty || (KernelMemory::pageState(
                                                    parent, source.address) &
                                                (1ULL << 6));
                copy->dirty  = source.dirty;

                // 父子都先失去直接写权限；任意一方第一次写入时会再次缺页，届时才生成私有副本。
                copy->cow = source.cow = true;

                if ((source.mapped && os::detail::MemoryControl().protectTarget(
                                          source.address, 4096, 4, parent)) ||
                    (copy->mapped && os::detail::MemoryControl().protectTarget(
                                         copy->address, 4096, 4, child))) {
                    state_.releaseTask(child);

                    return -11;
                }
            }
        }

        // 只有页面记录和句柄都准备好，才提交新区域表。
        state_.regions = staged;

        return 0;
    }

    /*
     * 当进程写入共享的私有页时，分配新页并通过临时映射复制内容。
     * 新页安装成功后才替换旧句柄，失败时尝试恢复原来的只读映射。
     */
    int Pager::copyPrivate(ResidentPage& page, const Region& region) {
        // 把旧页和新页临时映射到服务自己的两个相邻地址，复制完立即撤销，目标进程此时仍保留旧页。
        constexpr uint64_t source = 0x60000000, destination = source + 4096;
        const int64_t handle = allocator_.allocate(page.task);

        if (handle <= 0) {
            return handle;
        }

        if (os::detail::MemoryControl().mapPages(page.handle, source, 4096, 0,
                                                 0) != source) {
            os::closeHandle(handle);

            return -5;
        }

        if (os::detail::MemoryControl().mapPages(handle, destination, 4096, 0,
                                                 1) != destination) {
            os::detail::MemoryControl().unmapTarget(source, 4096);
            os::closeHandle(handle);

            return -5;
        }

        os::Runtime::copyBytes(reinterpret_cast<void*>(destination),
                               reinterpret_cast<void*>(source), 4096);
        os::detail::MemoryControl().unmapTarget(destination, 4096);
        os::detail::MemoryControl().unmapTarget(source, 4096);
        const uint64_t protection =
            KernelMemory::nativeProtection(region.protection);

        // 临时复制已完成，接下来才从目标地址撤下旧页；保留锁页记录，替换后仍应满足原锁定约定。
        if (page.mapped && os::detail::MemoryControl().unmapTarget(
                               page.address, 4096, page.task, true)) {
            os::closeHandle(handle);

            return -11;
        }

        page.mapped = false;

        // 安装新页失败时尝试把旧页按只读方式放回，避免一次复制失败就让原数据完全不可访问。
        if (os::detail::MemoryControl().mapPages(handle, page.address, 4096, 0,
                                                 protection,
                                                 page.task) != page.address) {
            if (os::detail::MemoryControl().mapPages(
                    page.handle, page.address, 4096, 0, protection & ~1ULL,
                    page.task) == page.address) {
                page.mapped = true;
                os::detail::MemoryControl().protectTarget(page.address, 4096, 4,
                                                          page.task);
            }

            os::closeHandle(handle);

            return -5;
        }

        // 新映射可靠安装以后才关闭旧页句柄，之前所有失败路径都还需要它做恢复。
        os::closeHandle(page.handle);

        // 新映射已经安装成功，再把页面记录切换到新句柄；此前失败仍可沿用旧页。
        page.handle  = handle;
        page.cow     = false;
        page.dirty   = true;
        page.mapped  = true;
        page.lastUse = ++state_.clock;

        return 0;
    }

}  // namespace mm
