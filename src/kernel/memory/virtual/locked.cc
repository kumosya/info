/*
 * src/kernel/memory/virtual/locked.cc
 * 用有序区间管理锁页范围，合并或拆分时先准备后提交。
 */

#include "kernel/task.h"

namespace task {

    /*
     * 检查地址是否落在任一锁页区间内。
     * 区间采用左闭右开形式，终点地址属于下一个区间。
     */
    bool AddressSpace::Residency::contains(uint64_t address) const {
        for (unsigned i = 0; i < count; ++i) {
            if (address >= ranges[i].begin && address < ranges[i].end) {
                return true;
            }
        }

        return false;
    }

    /*
     * 计算给定范围与已锁页区间的重叠字节数，用来判断新增锁页会消耗多少额度。
     */
    uint64_t AddressSpace::Residency::overlap(uint64_t address,
                                              uint64_t size) const {
        uint64_t bytes = 0;

        for (unsigned i = 0; i < count; ++i) {
            const uint64_t begin =
                address > ranges[i].begin ? address : ranges[i].begin;
            const uint64_t end =
                address + size < ranges[i].end ? address + size : ranges[i].end;

            if (begin < end) {
                bytes += end - begin;
            }
        }

        return bytes;
    }

    /*
     * 增加或删除锁页区间。
     * 先在临时表中合并相邻区间或切开重叠区间，容量足够才整体替换原表，失败时保留原记录。
     */
    bool AddressSpace::Residency::set(uint64_t address, uint64_t size,
                                      bool locked) {
        if (!size || address > UINT64_MAX - size) {
            return false;
        }

        // 在临时表中拼出新结果，任何容量不足都直接返回，不破坏原有锁页区间。
        Residency next;

        // 把追加及相邻合并集中在一个具名辅助对象中，明确保存它需要修改的目标表。
        struct RangeEmitter {
                Residency& next;

                /* 追加非空区间，能与末尾相邻时直接合并；容量不足时不越界写入。
                 */
                bool operator()(uint64_t begin, uint64_t end) const {
                    if (begin >= end) {
                        return true;
                    }

                    if (next.count &&
                        next.ranges[next.count - 1].end == begin) {
                        next.bytes += end - begin;
                        next.ranges[next.count - 1].end = end;

                        return true;
                    }

                    if (next.count == 128) {
                        return false;
                    }

                    next.ranges[next.count++] = {begin, end};
                    next.bytes += end - begin;

                    return true;
                }
        } emit{next};

        uint64_t begin = address, end = address + size;

        // 记录新增区间是否已经插入，遍历到后方不相交区间时只插入一次，避免重复累计锁页字节。
        bool inserted = false;

        for (unsigned i = 0; i < count; ++i) {
            const Range old = ranges[i];

            // 加锁把重叠或相邻区间合并；解锁则保留删除范围外的左右片段，两种操作都先写临时表。
            if (locked) {
                if (old.end < begin) {
                    if (!emit(old.begin, old.end)) {
                        return false;
                    }
                } else if (old.begin > end) {
                    if (!inserted) {
                        if (!emit(begin, end)) {
                            return false;
                        }

                        inserted = true;
                    }

                    if (!emit(old.begin, old.end)) {
                        return false;
                    }
                } else {
                    if (old.begin < begin) {
                        begin = old.begin;
                    }

                    if (old.end > end) {
                        end = old.end;
                    }
                }
            } else if (old.end <= begin || old.begin >= end) {
                if (!emit(old.begin, old.end)) {
                    return false;
                }
            } else {
                if (old.begin < begin && !emit(old.begin, begin)) {
                    return false;
                }

                if (old.end > end && !emit(end, old.end)) {
                    return false;
                }
            }
        }

        if (locked && !inserted && !emit(begin, end)) {
            return false;
        }

        // 所有区间都处理成功后一次性提交，并同时更新累计锁页字节数。
        *this = next;
        return true;
    }

}  // namespace task
