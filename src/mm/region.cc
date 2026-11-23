/*
 * src/mm/region.cc
 * 维护虚拟区域的查找、空洞选择、拆分、删除和合并。
 */

#include "service/mm/mm.h"

namespace mm {

    /*
     * 检查区间非空、页面对齐、地址加长度不溢出，并完全位于允许的用户地址范围。
     */
    bool RegionTable::validRange(uint64_t address, uint64_t size) {
        return size && !((address | size) & 4095) && address >= lower &&
               address < upper && size <= upper - address;
    }

    /*
     * 按任务编号和地址查找包含该地址的有效区域，只返回读取视图。
     */
    const Region* RegionTable::find(uint64_t task, uint64_t address) const {
        for (const Region& r : regions_) {
            if (r.used && r.task == task && address >= r.address &&
                address - r.address < r.size) {
                return &r;
            }
        }

        return nullptr;
    }

    /*
     * 复用只读查找规则取得可修改区域，确保两种查找的边界判断一致。
     */
    Region* RegionTable::find(uint64_t task, uint64_t address) {
        return const_cast<Region*>(
            static_cast<const RegionTable*>(this)->find(task, address));
    }

    /*
     * 寻找一个未使用的区域槽位，没有空位时返回空指针。
     */
    Region* RegionTable::vacant() {
        for (Region& r : regions_) {
            if (!r.used) {
                return &r;
            }
        }

        return nullptr;
    }

    /*
     * 统计区域表剩余槽位，供需要拆分区域的操作提前检查容量。
     */
    size_t RegionTable::vacancies() const {
        size_t count = 0;

        for (const Region& r : regions_) {
            if (!r.used) {
                ++count;
            }
        }

        return count;
    }

    /*
     * 从提示地址开始寻找足够大的空闲虚拟区间，跳过与该任务现有区域重叠的范围。
     */
    uint64_t RegionTable::findGap(uint64_t task, uint64_t size,
                                  uint64_t hint) const {
        if (!size || (size & 4095) || hint > upper - 4096) {
            return 0;
        }

        // 将提示地址向上对齐到页边界，并保证不会落入低地址保护区。
        uint64_t candidate = (hint + 4095) & ~4095ULL;

        if (candidate < lower) {
            candidate = lower;
        }

        while (validRange(candidate, size)) {
            uint64_t next = candidate;

            // 找出与候选区间重叠的现有区域，把下次起点推到这些区域的末尾，而不是逐页碰运气。
            for (const Region& r : regions_) {
                if (r.used && r.task == task && r.address < candidate + size &&
                    candidate < r.address + r.size &&
                    next < r.address + r.size) {
                    next = r.address + r.size;
                }
            }

            // 扫描一轮没有任何区域推动起点，说明整段候选空间都未被占用。
            if (next == candidate) {
                return candidate;
            }

            candidate = next;
        }

        return 0;
    }

    /*
     * 验证新区域范围及重叠关系，再占用空槽登记；兼容的相邻区域可由合并路径减少记录数。
     */
    bool RegionTable::insert(const Region& region) {
        if (!region.task || !validRange(region.address, region.size) ||
            (region.offset & 4095) ||
            region.offset > UINT64_MAX - region.size ||
            (region.protection & ~7U) || (region.maximumProtection & ~7U) ||
            (region.protection & ~region.maximumProtection) ||
            (region.protection & 6U) == 6U) {
            return false;
        }

        // 同一任务的区域不能重叠；不同任务可使用相同虚拟地址，所以比较时同时核对任务编号。
        for (const Region& r : regions_) {
            if (r.used && r.task == region.task &&
                r.address < region.address + region.size &&
                region.address < r.address + r.size) {
                return false;
            }
        }

        // 校验全部通过才占用空槽，容量不足时原区域表保持不变。
        Region* slot = vacant();

        if (!slot) {
            return false;
        }

        *slot = region;
        slot->used = true;

        // 新区域可能恰好连接两段同属性区域，提交后做合并可以节省后续拆分所需槽位。
        merge(region.task);

        return true;
    }

    /*
     * 检查指定任务的整个范围是否被区域连续覆盖，不能只验证首尾地址。
     */

    // 连续覆盖允许跨越多个相邻区域，但中间不能出现未登记的地址空洞。
    bool RegionTable::covered(uint64_t task, uint64_t address,
                              uint64_t size) const {
        if (!validRange(address, size)) {
            return false;
        }

        const uint64_t end = address + size;

        // 每次查找覆盖当前游标的区域，再跳到该区域末尾；任何一次查不到就说明中间有洞。
        while (address < end) {
            const Region* r = find(task, address);

            if (!r) {
                return false;
            }

            address = r->address + r->size;
        }

        return true;
    }

    /*
     * 删除指定范围的区域记录。
     * 完整覆盖则删除，局部覆盖则缩短或拆开；先准备结果，容量不足时保留原表。
     */
    bool RegionTable::remove(uint64_t task, uint64_t address, uint64_t size) {
        if (!validRange(address, size)) {
            return false;
        }

        const uint64_t end = address + size;

        // 第一轮只检查是否需要额外槽位，先把容量不足的问题发现出来，再进入实际修改。
        for (const Region& r : regions_) {
            if (r.used && r.task == task && address > r.address &&
                end < r.address + r.size && !vacancies()) {
                return false;
            }
        }

        // 第二轮只处理与删除范围相交的本任务区域，其他任务和不相交区域不受影响。
        for (Region& r : regions_) {
            if (!r.used || r.task != task || r.address >= end ||
                address >= r.address + r.size) {
                continue;
            }

            const uint64_t oldEnd = r.address + r.size;

            if (address <= r.address && end >= oldEnd) {
                // 删除范围覆盖整条区域时，直接清空记录，不为它保留左右片段。
                r = {};
            } else if (address > r.address && end < oldEnd) {
                // 从区域中间删除会留下左右两段；右段要同步前移文件偏移，才能仍然指向原数据。
                Region* right = vacant();
                *right = r;
                right->address = end;
                right->size = oldEnd - end;
                right->offset += end - r.address;
                r.size = address - r.address;
            } else if (address <= r.address) {
                // 删除前缀时，虚拟地址起点和后备对象偏移必须同时移动，否则余下区域会读取错误位置。
                r.offset += end - r.address;
                r.address = end;
                r.size = oldEnd - end;
            } else {
                r.size = address - r.address;
            }
        }

        merge(task);

        return true;
    }

    /*
     * 按新权限拆分并更新区域。
     * 检查覆盖和容量后提交，再合并兼容的相邻记录。
     */
    bool RegionTable::protect(uint64_t task, uint64_t address, uint64_t size,
                              uint32_t protection) {
        if ((protection & ~7U) || (protection & 6U) == 6U ||
            !covered(task, address, size)) {
            return false;
        }

        const uint64_t end = address + size;

        // 先统计每个受影响区域需要几条额外记录：改中间一段最多要保留左、中、右三段。
        size_t required = 0;

        for (const Region& r : regions_) {
            if (!r.used || r.task != task || r.address >= end ||
                address >= r.address + r.size) {
                continue;
            }

            // 新权限只能在区域原先的上限内调整，不能把只读文件映射改成可写。
            if (protection & ~r.maximumProtection) {
                return false;
            }

            if (address > r.address) {
                ++required;
            }

            if (end < r.address + r.size) {
                ++required;
            }
        }

        if (vacancies() < required) {
            return false;
        }

        // 容量足够以后才开始拆分；先切左边，再切右边，最后只给中间交集修改权限。
        for (size_t i = 0; i < capacity; ++i) {
            Region* r = &regions_[i];

            if (!r->used || r->task != task || r->address >= end ||
                address >= r->address + r->size) {
                continue;
            }

            if (address > r->address) {
                Region* middle = vacant();
                *middle = *r;
                middle->address = address;
                middle->size -= address - r->address;
                middle->offset += address - r->address;
                r->size = address - r->address;
                r = middle;
            }

            if (end < r->address + r->size) {
                Region* right = vacant();
                *right = *r;
                right->address = end;
                right->size -= end - r->address;
                right->offset += end - r->address;
                r->size = end - r->address;
            }

            // r 此时已经指向真正需要修改的交集，外围片段继续保留旧权限。
            r->protection = protection;
        }

        // 改完后重新合并连续同属性区域，避免反复改权限把记录表切得越来越碎。
        merge(task);

        return true;
    }

    /*
     * 合并同一任务中相邻、权限和后备关系兼容的区域，保持文件偏移连续，减少槽位占用。
     */
    void RegionTable::merge(uint64_t task) {
        bool changed;

        // 一次合并可能让更远的第三段也变成相邻，循环到没有变化才能得到最终结果。
        do {
            changed = false;

            for (Region& a : regions_) {
                if (a.used && a.task == task) {
                    for (Region& b : regions_) {
                        // 地址相邻还不够，后备偏移和权限等属性也必须连续兼容，否则合并会抹掉重要边界。
                        if (&a == &b || !b.used || b.task != task ||
                            a.address + a.size != b.address ||
                            a.offset + a.size != b.offset ||
                            a.backing != b.backing ||
                            a.protection != b.protection ||
                            a.maximumProtection != b.maximumProtection ||
                            a.kind != b.kind || a.lazy != b.lazy ||
                            a.shared != b.shared) {
                            continue;
                        }

                        // 由左段吞并右段并清空右槽，不移动实际页面，仅压缩管理记录。
                        a.size += b.size;
                        b = {};
                        changed = true;
                    }
                }
            }
        } while (changed);
    }

    /*
     * 清空属于某任务的全部区域记录，实际页面和后备对象由上层分别回收。
     */
    void RegionTable::removeTask(uint64_t task) {
        for (Region& r : regions_) {
            if (r.used && r.task == task) {
                r = {};
            }
        }
    }

    /*
     * 统计仍引用指定后备对象的区域数，决定后备对象能否被释放。
     */
    size_t RegionTable::references(uint64_t backing) const {
        size_t count = 0;

        for (const Region& r : regions_) {
            if (r.used && r.backing == backing) {
                ++count;
            }
        }

        return count;
    }

}  // namespace mm
