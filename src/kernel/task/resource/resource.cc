/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/resource/resource.cc
 * 管理资源上限、线程统计及经过权限检查的优先级修改。
 */

#include "kernel/task.h"
#include "service/common/records.h"

namespace task {

    /*
     * 查询或设置任务资源软硬上限。
     * 校验资源类别、软硬大小关系和调用者权限，普通任务不能擅自抬高硬上限。
     */
    int Task::resourceLimit(Task& caller, int resource, bool set,
                            uint64_t& soft, uint64_t& hard) {
        InterruptSpinLockGuard guard(lock_);

        if (&caller != this && caller.effectiveUid() != 0 &&
            (caller.uid() != uid_ || caller.uid() != effectiveUid_ ||
             caller.uid() != savedUid_ || caller.gid() != gid_ ||
             caller.gid() != effectiveGid_ || caller.gid() != savedGid_)) {
            return 1;
        }

        if (resource != 4 && resource != 8 && resource != 13) {
            return resource >= 0 && resource < 16 ? 95 : 22;
        }

        uint64_t& current      = resource == 4   ? coreLimit_
                                 : resource == 8 ? memlockLimit_
                                                 : niceLimit_;
        uint64_t& maximum      = resource == 4   ? coreHardLimit_
                                 : resource == 8 ? memlockHardLimit_
                                                 : niceHardLimit_;
        const uint64_t oldSoft = current, oldHard = maximum;

        // 查询不改变配置；修改则先检查软硬上限关系，再核对调用者能否提升硬上限。
        if (set) {
            if (soft > hard) {
                return 22;
            }

            if (hard > maximum && caller.effectiveUid() != 0) {
                return 1;
            }

            __atomic_store_n(&current, soft, __ATOMIC_RELEASE);
            maximum = hard;
        }

        soft = oldSoft;
        hard = oldHard;

        return 0;
    }

    /*
     * 在任务锁内读取主线程的 nice 值；没有主线程时使用默认值。
     */
    int32_t Task::mainNice() {
        InterruptSpinLockGuard guard(lock_);

        return mainThread_ ? mainThread_->getNice() : 0;
    }

    /*
     * 在任务锁内取得主线程编号，没有主线程时返回零。
     */
    uint64_t Task::mainThreadId() {
        InterruptSpinLockGuard guard(lock_);

        return mainThread_ ? mainThread_->getId() : 0;
    }

    /*
     * 在任务锁内读取线程数量，避免与创建、退出的计数更新冲突。
     */
    uint32_t Task::threadCount() {
        InterruptSpinLockGuard guard(lock_);
        uint32_t count = 0;

        for (Thread* thread = threads_; thread;
             thread         = thread->getTaskNext()) {
            if (thread->getState() != ThreadState::Terminated) {
                ++count;
            }
        }

        return count;
    }

    /*
     * 查找编号不小于给定游标的最小线程，并复制其状态、优先级和 CPU
     * 等信息，供按编号枚举。
     */
    bool Task::threadInfo(uint64_t minimumTid, services::wire::ThreadInfo& info) {
        InterruptSpinLockGuard guard(lock_);

        // 用最小 TID 选择下一条记录，而不是返回某个不稳定的链表下标。
        Thread* selected = nullptr;

        for (Thread* thread = threads_; thread;
             thread         = thread->getTaskNext()) {
            if (thread->getId() >= minimumTid &&
                thread->getState() != ThreadState::Terminated &&
                (!selected || thread->getId() < selected->getId())) {
                selected = thread;
            }
        }

        if (!selected) {
            return false;
        }

        // 找到目标后在任务锁内一次填好信息，返回后调用者无需继续持有线程裸指针。
        info = {selected->getId(), id_, selected->getRuntime(),
                selected->getNice(),
                static_cast<uint32_t>(selected->getState())};

        return true;
    }

    /*
     * 按进程、进程组或用户选择目标并查询或调整 nice。
     * 每个目标单独核对身份和资源限制，返回合适的查询值或操作结果。
     */
    int applyTaskPriority(Task& caller, Thread& current, unsigned operation,
                          unsigned which, uint64_t who, int value,
                          int& result) {
        if (which > 2 || operation > 2 || (operation == 2 && (which || who))) {
            return 22;
        }

        if (!who) {
            who = which == 0   ? current.getId()
                  : which == 1 ? caller.taskGroupId()
                               : caller.uid();
        }

        const bool privileged = caller.effectiveUid() == 0;
        bool found            = false;
        int error             = 0;
        result                = 20;
        uint64_t cursor       = 0;

        // 按进程、进程组或用户的选择规则遍历目标，再对每个符合条件的线程分别查询或调整。
        while (true) {
            ::task::TaskReference target = task::next(cursor);

            if (!target) {
                break;
            }

            cursor = target->id() + 1;

            if (which == 1 && target->taskGroupId() != who) {
                continue;
            }

            InterruptSpinLockGuard guard(target->lock_);

            if (which == 2 && target->uid_ != who) {
                continue;
            }

            for (Thread* thread = target->threads_; thread;
                 thread         = thread->getTaskNext()) {
                if ((which == 0 && thread->getId() != who) ||
                    thread->getState() == ThreadState::Terminated) {
                    continue;
                }

                found = true;
                // 一组线程的优先级查询返回最小
                // nice，也就是最高优先级，不能取平均值或随便选第一个。
                if (!operation) {
                    const int nice = thread->getNice();

                    if (nice < result) {
                        result = nice;
                    }

                    continue;
                }

                if (!privileged && caller.effectiveUid() != target->uid_ &&
                    caller.effectiveUid() != target->effectiveUid_) {
                    error = 1;
                    continue;
                }

                int changed = 0;
                // 使用目标任务的 NICE 限额和已认证调用者的特权；实际权重修改与
                // CPU 记账在同一调度锁保护下完成。
                const int status =
                    task::changeNice(*thread, value, operation == 2, privileged,
                                     target->niceLimit_, changed);

                if (status) {
                    error = status;
                } else {
                    result = changed;
                }
            }

            if (which == 0 && found) {
                break;
            }
        }

        // 没有任何匹配目标时返回明确错误，不能把默认初始值误当成实际查到的优先级。
        return found ? error : 3;
    }

    /*
     * 校验核心转储软硬上限并更新任务配置，软上限不得超过硬上限。
     */
    bool Task::setCoreLimit(uint64_t soft, uint64_t hard) {
        InterruptSpinLockGuard guard(lock_);

        if (soft > hard || (hard > coreHardLimit_ && effectiveUid_ != 0)) {
            return false;
        }

        coreLimit_     = soft;
        coreHardLimit_ = hard;

        return true;
    }

}  // namespace task
