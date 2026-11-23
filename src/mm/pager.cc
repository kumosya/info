/*
 * src/mm/pager.cc
 * 按区域权限处理缺页，读取文件页面并建立实际映射。
 */

#include <os/detail/client.h>
#include <os/file.h>
#include <os/runtime.h>

#include "service/mm/mm.h"

namespace mm {

    /*
     * 把文件对应的一页读入页对象。
     * 临时映射页对象到服务自己的地址空间，按文件偏移读取，所有返回路径都撤销临时映射。
     */
    int Pager::loadFile(ResidentPage& page, const Region& region,
                        Backing& backing) {
        constexpr uint64_t scratch = 0x60000000;

        // 文件后备必须同时有文件服务和描述符；缺任一项都不能把未读入页面当有效文件内容。
        if (!backing.descriptor || !backing.fs) {
            return -11;
        }

        if (static_cast<int64_t>(os::detail::MemoryControl().mapPages(
                page.handle, scratch, 4096, 0, 1)) !=
            static_cast<int64_t>(scratch)) {
            return -5;
        }

        // 区域起点不一定是文件开头；文件读取偏移等于区域文件偏移加页地址在区域内的距离。
        const uint64_t offset = region.offset + page.address - region.address;
        os::File file;

        if (file.borrow(backing.fs, backing.descriptor, nullptr, O_RDONLY)) {
            os::detail::MemoryControl().unmapTarget(scratch, 4096);

            return -11;
        }

        // 每次补页重新确认文件大小，映射后文件可能已被缩短，越过新文件末尾要报告对应错误。
        if (file.refresh() || offset >= file.getSize()) {
            os::detail::MemoryControl().unmapTarget(scratch, 4096);

            return -18;
        }

        // 读文件期间标记正在进行 I/O，回收器必须跳过这一页。
        page.io = true;
        const int64_t read =
            file.pread(reinterpret_cast<void*>(scratch), 4096, offset);
        page.io = false;

        // 不论读取成功与否都撤销服务内临时映射，避免下次补页撞到旧地址。
        const int unmapped =
            os::detail::MemoryControl().unmapTarget(scratch, 4096);
        page.fileOffset = offset;

        return read < 0 || unmapped ? -11 : 0;
    }

    /*
     * 为锁页请求提前准备实际页面。
     * 即使区域当前禁止访问，也可以保存其后备页，但不会擅自授予用户读写权限。
     */
    int Pager::populateLocked(uint64_t task, uint64_t address) {
        const Region* region = state_.regions.find(task, address);

        if (!region) {
            return -4;
        }

        // 区域本来允许访问时复用普通缺页路径；禁止访问区域的锁页准备不能擅自给用户加访问权限。
        if (region->protection) {
            return fault(task, address,
                         (region->protection & 1)   ? 0
                         : (region->protection & 4) ? 16
                                                    : 2);
        }

        address &= ~4095ULL;

        if (ResidentPage* existing = state_.page(task, address)) {
            return existing->used ? 0 : -4;
        }

        if (state_.pagesUsed(task) >= MemoryState::taskPageLimit) {
            return -5;
        }

        ResidentPage* page = state_.vacantPage();
        Backing* backing = state_.backing(region->backing);

        if (!page || !backing) {
            return -5;
        }

        // 先占驻留记录，再取得后备页；后续分配或文件读取失败时把本次记录清回空槽。
        *page = {};
        page->used = true;
        page->task = task;
        page->address = address;
        page->backing = region->backing;

        if (backing->kind != BackingKind::Shared) {
            const int64_t handle = allocator_.allocate(task);

            if (handle <= 0) {
                *page = {};
                return -5;
            }

            page->handle = handle;

            if (backing->kind == BackingKind::File) {
                const int error = loadFile(*page, *region, *backing);

                if (error) {
                    os::closeHandle(page->handle);
                    *page = {};
                    return error;
                }
            }
        }

        return 0;
    }

    /*
     * 处理缺页请求。
     * 先核对区域和访问权限，再按共享、文件或匿名来源取得页面；写时复制页面的写访问转交私有复制路径。
     */
    int Pager::fault(uint64_t task, uint64_t address, uint64_t flags) {
        const Region* region = state_.regions.find(task, address);

        if (!region) {
            return -4;
        }

        // 按执行、写入、读取分别检查区域权限，拒绝的访问不能靠“补一页”绕过权限限制。
        if ((flags & 16)  ? !(region->protection & 4)
            : (flags & 2) ? !(region->protection & 2)
                          : !(region->protection & 1)) {
            return -3;
        }

        address &= ~4095ULL;
        const uint64_t protection =
            KernelMemory().nativeProtection(region->protection);
        ResidentPage* page = state_.page(task, address);

        // 写时复制不是普通权限错误：读取仍共享原页，写入才分配并复制私有页。
        if (page && page->cow && (flags & 2)) {
            return copyPrivate(*page, *region);
        }

        if (page && page->mapped) {
            // 管理记录说已映射还不够，重新读取内核页表确认；已有却权限不符时应报错，不能偷偷放宽。
            const uint64_t actual = KernelMemory().pageState(task, address);

            if ((actual & 1) && (!(flags & 2) || (actual & 2)) &&
                (!(flags & 16) || !(actual & (1ULL << 63)))) {
                // 记录访问次序，供后续页面策略判断活跃程度；这不是墙上时钟时间。
                page->lastUse = ++state_.clock;

                return 0;
            }

            if (actual & 1) {
                return -3;
            }

            page->mapped = false;
        }

        // 新建记录与复用旧记录的失败处理不同：只能清掉本次新建的记录，不能误删原有页面。
        bool fresh = page == nullptr;

        if (fresh) {
            if (state_.pagesUsed(task) >= MemoryState::taskPageLimit) {
                return -5;
            }

            page = state_.vacantPage();

            // 驻留记录耗尽时先尝试回收一页再重取空槽，仍无空槽就停止，不能覆盖已有记录。
            if (!page) {
                reclaim_.reclaim(4096);
                page = state_.vacantPage();
            }

            if (!page) {
                return -5;
            }

            *page = {};
            page->used = true;
            page->task = task;
            page->address = address;
            page->backing = region->backing;
        }

        Backing* backing = state_.backing(region->backing);

        if (!backing) {
            if (fresh) {
                *page = {};
            }

            return -2;
        }

        // 共享页由共享对象管理，文件和匿名页则使用本服务持有的页对象句柄。
        if (backing->kind == BackingKind::Shared) {
            const int result = KernelMemory().mapShared(
                backing->handle, task, address, 4096,
                region->offset + address - region->address, protection);

            if (result) {
                if (fresh) {
                    *page = {};
                }

                return result;
            }
        } else {
            // 普通匿名页和文件页都需要页对象；文件页首次取得对象后，还必须把对应文件内容读进去。
            if (!page->handle) {
                const int64_t handle = allocator_.allocate(task);

                if (handle <= 0) {
                    if (fresh) {
                        *page = {};
                    }

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
                    page->cow ? protection & ~1ULL : protection,
                    task) != address) {
                if (fresh) {
                    os::closeHandle(page->handle);
                    *page = {};
                }

                return -5;
            }

            // 共享私有页保持只读并带写时复制标记，直到真正写入时再由
            // copyPrivate 分配新页。
            if (page->cow && os::detail::MemoryControl().protectTarget(
                                 address, 4096, 4, task)) {
                return -11;
            }
        }

        page->mapped = true;

        // 记录访问次序，供后续页面策略判断活跃程度；这不是墙上时钟时间。
        page->lastUse = ++state_.clock;

        return 0;
    }

}  // namespace mm
