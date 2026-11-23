/*
 * src/mm/state.cc
 * 管理区域、驻留页、后备对象和任务映像代号，清理过期记录。
 */

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

    /*
     * 按任务和页地址查找驻留记录。
     * 这里只查服务元数据，不会触发实际补页。
     */
    ResidentPage* MemoryState::page(uint64_t task, uint64_t address) {
        for (ResidentPage& p : pages) {
            if (p.used && p.task == task && p.address == address) {
                return &p;
            }
        }

        return nullptr;
    }

    /*
     * 返回第一个未占用的驻留页槽位，没有空间时返回空指针。
     */
    ResidentPage* MemoryState::vacantPage() {
        for (ResidentPage& p : pages) {
            if (!p.used) {
                return &p;
            }
        }

        return nullptr;
    }

    /*
     * 按后备编号查找有效后备记录，供区域和驻留页解析数据来源。
     */
    Backing* MemoryState::backing(uint64_t id) {
        for (Backing& b : backings) {
            if (b.used && b.id == id) {
                return &b;
            }
        }

        return nullptr;
    }

    /*
     * 占用一个空后备槽，清零旧字段并分配新编号，避免复用槽位时沿用旧资源信息。
     */
    Backing* MemoryState::createBacking(BackingKind kind) {
        for (Backing& b : backings) {
            if (!b.used) {
                // 空槽复用前完整清零，旧文件句柄、共享对象或租用编号不能带进新后备对象。
                b      = {};
                b.used = true;
                b.kind = kind;
                b.id   = nextBacking_++;

                return &b;
            }
        }

        return nullptr;
    }

    /*
     * 统计一个任务占用的驻留记录数，供逐任务页面上限检查使用。
     */
    uint64_t MemoryState::pagesUsed(uint64_t task) const {
        uint64_t count = 0;

        for (const ResidentPage& p : pages) {
            if (p.used && p.task == task) {
                ++count;
            }
        }

        return count;
    }

    /*
     * 检查区域或驻留页是否仍引用某个后备对象，两类引用都不存在时才允许释放。
     */
    bool MemoryState::references(uint64_t id) const {
        if (regions.references(id)) {
            return true;
        }

        for (const ResidentPage& page : pages) {
            if (page.used && page.backing == id) {
                return true;
            }
        }

        return false;
    }

    /*
     * 回收无人引用的后备对象。
     * 文件租用要先向文件服务归还，句柄及描述符关闭成功后才清空记录。
     */
    void MemoryState::collectBackings(bool releaseFiles) {
        for (Backing& b : backings) {
            if (!b.used || regions.references(b.id)) {
                continue;
            }

            // 区域已经删除也可能仍有驻留页引用，必须同时检查两类使用者，不能只看区域表。
            bool live = false;

            for (const ResidentPage& p : pages) {
                if (p.used && p.backing == b.id) {
                    live = true;
                }
            }

            if (live) {
                continue;
            }

            // 文件映射租用也属于后备对象资源；文件服务拒绝归还时先保留记录，不能假装已经释放。
            if (b.lease) {
                if (!releaseFiles) {
                    continue;
                }

                os::IpcMessage request{}, reply{};
                request.type =
                    static_cast<uint64_t>(os::FsRequest::ReleaseMapping);
                request.length  = 1;
                request.data[0] = b.lease;

                if (!b.fs || os::callService(b.fs, request, reply) ||
                    static_cast<int64_t>(reply.data[0])) {
                    continue;
                }
            }

            // 文件租用和本地描述符分别关闭，某一步失败时保留足够记录，让以后有机会继续清理。
            if (b.descriptor) {
                os::File file;

                if (file.adopt(b.fs, b.descriptor, nullptr, O_RDONLY) ||
                    file.close()) {
                    continue;
                }

                b.descriptor = 0;
            }

            if (b.handle) {
                os::closeHandle(b.handle);
            }

            if (b.fs) {
                os::closeHandle(b.fs);
            }

            // 所有关联资源都成功归还后再清槽，避免丢失仍需关闭的句柄。
            b = {};
        }
    }

    /*
     * 删除任务的代号、区域和驻留页记录，关闭页面句柄后再收集无人使用的后备对象。
     */
    void MemoryState::releaseTask(uint64_t task) {
        for (Domain& domain : domains_) {
            if (domain.task == task) {
                domain = {};
            }
        }

        // 先移除区域和任务代号，再关闭该任务所有页句柄；后备对象最后统一检查是否还有其他任务引用。
        regions.removeTask(task);

        for (ResidentPage& p : pages) {
            if (p.used && p.task == task) {
                if (p.handle) {
                    os::closeHandle(p.handle);
                }

                p = {};
            }
        }

        collectBackings();
    }

}  // namespace mm

namespace mm {

    /*
     * 将服务记录的任务映像代号与内核对齐。
     * 任务不存在或已经 exec 时清理旧记录，防止旧虚拟地址被误用于新程序。
     */
    int MemoryState::synchronize(uint64_t task) {
        os::wire::MemoryUsage usage{};

        if (os::detail::MemoryQuery().usage(usage, task)) {
            releaseTask(task);

            return -4;
        }

        for (Domain& domain : domains_) {
            if (domain.task == task) {
                // PID
                // 没变也可能已经执行新程序，只有页表映像代号相同才能继续沿用旧记录。
                if (domain.generation == usage.generation) {
                    return 0;
                }

                releaseTask(task);
                domain = {task, usage.generation};

                return 1;
            }
        }

        for (Domain& domain : domains_) {
            if (!domain.task) {
                domain = {task, usage.generation};

                return 0;
            }
        }

        return -5;
    }

    /*
     * 定期检查登记任务是否退出或更换映像，清理过期内存记录，并返回是否发生状态变化。
     */
    bool MemoryState::sweep() {
        bool changed = false;

        for (Domain& domain : domains_) {
            if (domain.task) {
                // 先判断任务是否已退出，再检查内存映像代号，覆盖同一 PID
                // 执行新程序的情况。
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
        }

        return changed;
    }

    /*
     * 恢复后剔除任务或代号已经失效的记录。
     * 此时旧句柄尚需按恢复规则处理，不能直接套用普通退出路径。
     */
    void MemoryState::pruneRecovered() {
        for (Domain& domain : domains_) {
            if (domain.task) {
                os::wire::MemoryUsage usage{};

                if (!os::detail::MemoryQuery().usage(usage, domain.task) &&
                    usage.generation == domain.generation) {
                    continue;
                }

                regions.removeTask(domain.task);

                for (ResidentPage& page : pages) {
                    if (page.used && page.task == domain.task) {
                        page = {};
                    }
                }

                domain = {};
            }
        }

        for (Backing& backing : backings) {
            if (backing.used && !references(backing.id)) {
                backing = {};
            }
        }
    }

}  // namespace mm
