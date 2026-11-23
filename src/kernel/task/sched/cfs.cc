/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sched/cfs.cc
 * 按权重累计虚拟运行时间，管理入队、CPU 迁移与优先级。
 */

#include "kernel/cpu.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace task {

    // 索引为 nice 加 20，nice 为零时权重为 1024；nice
    // 越低，同样虚拟时间可获得更多真实 CPU 时间。
    static constexpr uint32_t niceWeights[40] = {
        88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
        9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
        1024,  820,   655,   526,   423,   335,   272,   215,   172,   137,
        110,   87,    70,    56,    45,    36,    29,    23,    18,    15};

    /*
     * 按调度权重把真实执行时间换算成虚拟运行时间。
     * 权重大时同样的真实时间只增加较少虚拟时间，因此以后更容易被选中。
     */
    static constexpr uint64_t calculateVruntimeDelta(uint64_t runtime,
                                                     uint32_t weight) {
        return (runtime * 1024ULL + weight - 1) / weight;
    }

    static_assert(calculateVruntimeDelta(1000000, niceWeights[0]) <
                  calculateVruntimeDelta(1000000, niceWeights[20]));
    static_assert(calculateVruntimeDelta(1000000, niceWeights[20]) <
                  calculateVruntimeDelta(1000000, niceWeights[39]));

    /*
     * 把可运行线程加入合适 CPU 的运行队列。
     * 检查状态、CPU 亲和性和重复入队，必要时通知目标 CPU 重新选择线程。
     */
    bool enqueueThread(Thread* thread) {
        if (thread == nullptr || thread->getInRunQueue() ||
            thread->getState() == ThreadState::Terminated) {
            return thread && thread->getState() != ThreadState::Terminated;
        }

        uint32_t targetCpuId           = thread->getCpuId();
        ::cpu::CpuLocalData* targetCpu = cpu::getCpuLocalData(targetCpuId);

        if (targetCpu == nullptr || targetCpu->getRunQueue() == nullptr ||
            (thread->getCpuAffinity() & (1ULL << targetCpuId)) == 0) {
            targetCpu = nullptr;

            // 目标 CPU
            // 必须同时满足在线集合与线程亲和性，不能只按负载挑选被禁止的 CPU。
            for (uint32_t cpuId = 0;
                 cpuId < cpu::getDiscoveredCpuCount() && cpuId < 64; ++cpuId) {
                ::cpu::CpuDescriptor* descriptor = cpu::getCpuDescriptor(cpuId);

                if ((thread->getCpuAffinity() & (1ULL << cpuId)) != 0 &&
                    descriptor != nullptr &&
                    descriptor->getState().load(MemoryOrder::Acquire) ==
                        ::cpu::CpuState::Online &&
                    descriptor->getLocalData() != nullptr &&
                    descriptor->getLocalData()->getRunQueue() != nullptr) {
                    targetCpu   = descriptor->getLocalData();
                    targetCpuId = cpuId;
                    break;
                }
            }
        }

        if (targetCpu == nullptr) {
            return false;
        }

        RunQueue* queue      = targetCpu->getRunQueue();
        const uint64_t flags = cpu::disableInterrupts();

        // 取得目标队列锁后重新检查线程是否已入队，前面的检查无法排除并发唤醒或终止。
        queue->getLock().lock();

        if (thread->getInRunQueue() ||
            thread->getState() == ThreadState::Terminated) {
            queue->getLock().unlock();
            cpu::restoreInterrupts(flags);

            return thread->getState() != ThreadState::Terminated;
        }

        // 内存恢复冻结也必须覆盖新加入线程；记录解冻后应恢复为就绪，但此时不能放入可运行树。
        if (thread->getTask() && thread->getTask()->memoryFrozen()) {
            thread->setStateBeforeStop(ThreadState::Ready);
            thread->setState(ThreadState::Stopped);
            queue->getLock().unlock();
            cpu::restoreInterrupts(flags);

            return true;
        }

        // 线程睡眠或从较早的虚拟时钟队列迁入，不能凭空积攒额外 CPU 份额。
        if (thread->getVruntime() < queue->getMinVruntime()) {
            thread->setVruntime(queue->getMinVruntime());
        }

        // 状态和树成员关系在目标队列锁内一起更新，不能让就绪线程漏出队列或重复入队。
        thread->setState(ThreadState::Ready);
        thread->setCpuId(targetCpuId);
        queue->insertNode(thread);
        queue->getLock().unlock();
        cpu::restoreInterrupts(flags);

        if (targetCpuId != cpu::getCurrentId()) {
            cpu::requestRemoteReschedule(targetCpuId);
        }

        return true;
    }

    /*
     * 从线程所属 CPU 的运行队列摘除线程，并同步更新就绪数量和最小虚拟时间。
     */
    void dequeueThread(Thread* thread) {
        if (thread == nullptr || !thread->getInRunQueue()) {
            return;
        }

        ::cpu::CpuLocalData* cpu = cpu::getCpuLocalData(thread->getCpuId());

        if (cpu == nullptr || cpu->getRunQueue() == nullptr) {
            return;
        }

        RunQueue* queue      = cpu->getRunQueue();
        const uint64_t flags = cpu::disableInterrupts();
        queue->getLock().lock();
        queue->removeNode(thread);
        queue->getLock().unlock();
        cpu::restoreInterrupts(flags);
    }

    /*
     * 从当前 CPU 取出下一条应运行的线程。
     * 普通队列为空时选择该 CPU 的空闲线程。
     */
    Thread* pickNextThread() {
        RunQueue* queue      = cpu::getCurrent()->getRunQueue();
        const uint64_t flags = cpu::disableInterrupts();
        queue->getLock().lock();
        Thread* thread = queue->getLeftmost() == nullptr
                             ? queue->getIdleThread()
                             : queue->getLeftmost();
        queue->getLock().unlock();
        cpu::restoreInterrupts(flags);

        return thread;
    }

    /*
     * 将 nice 值限制在允许范围内，再查表取得调度权重，数值越小的 nice
     * 通常对应越大的权重。
     */
    uint32_t getSchedulerWeight(int32_t nice) {
        if (nice < -20) {
            nice = -20;
        } else if (nice > 19) {
            nice = 19;
        }

        return niceWeights[nice + 20];
    }

    /*
     * 修改线程 nice 值。
     * 先核对权限和可提升额度，再在运行队列锁内更新排序相关状态，避免线程留在错误位置。
     */
    int changeNice(Thread& thread, int64_t value, bool relative,
                   bool privileged, uint64_t limit, int& result) {
        while (true) {
            const uint64_t id              = thread.getCpuId();
            ::cpu::CpuLocalData* targetCpu = cpu::getCpuLocalData(id);

            if (!targetCpu || !targetCpu->getRunQueue()) {
                return 3;
            }

            RunQueue& queue = *targetCpu->getRunQueue();
            InterruptSpinLockGuard guard(queue.getLock());

            if (id != thread.getCpuId()) {
                continue;
            }

            if (thread.getState() == ThreadState::Terminated) {
                return 3;
            }

            const int old = thread.getNice();
            int64_t next = relative ? static_cast<int64_t>(old) + value : value;

            // 先把外部优先级请求限制在支持范围，后面的权重查表才能使用合法索引。
            if (next < -20) {
                next = -20;
            }

            if (next > 19) {
                next = 19;
            }

            // NICE 限额用 20 减限额表示最低允许 nice；零不允许提升优先级，40
            // 及以上才允许达到 -20。
            const int floor = limit >= 40 ? -20 : 20 - static_cast<int>(limit);

            if (next < old && !privileged && (!limit || next < floor)) {
                return 13;
            }

            // 先按旧权重计算已经执行的时间，再改变权重，避免把过去时间按新规则重复记账。
            const uint64_t now     = time::getMonotonicTime();
            const uint64_t elapsed = now - queue.getClock();
            queue.setClock(now);
            Thread* running = queue.getCurrentThread();

            if (running && running != queue.getIdleThread() &&
                running->getState() == ThreadState::Running) {
                running->setRuntime(running->getRuntime() + elapsed);
                running->setSliceRuntime(running->getSliceRuntime() + elapsed);
                running->setVruntime(
                    running->getVruntime() +
                    calculateVruntimeDelta(elapsed, running->getWeight()));

                if (running->getTask()) {
                    running->getTask()->accountCpu(elapsed);
                }
            }

            thread.setNice(static_cast<int>(next));

            // 新权重只影响之后的运行时间，已经消耗的部分前面已经按旧权重记完。
            thread.setWeight(getSchedulerWeight(next));
            targetCpu->setRescheduleRequested(true);

            if (id != cpu::getCurrentId()) {
                cpu::requestRemoteReschedule(id);
            }

            result = static_cast<int>(next);

            return 0;
        }
    }

    /*
     * 把线程迁移到指定 CPU。
     * 检查目标是否合法以及亲和性是否允许，并协调新旧运行队列。
     */
    bool Thread::migrate(uint32_t cpu_id) {
        ::cpu::CpuDescriptor* target = cpu::getCpuDescriptor(cpu_id);

        if (cpu_id >= 64 || target == nullptr ||
            target->getState().load(MemoryOrder::Acquire) !=
                ::cpu::CpuState::Online ||
            (cpuAffinity & (1ULL << cpu_id)) == 0 ||
            state != ThreadState::Ready) {
            return false;
        }

        task::dequeueThread(this);
        cpuId = cpu_id;
        // 加入目标队列成功就是操作成功；函数返回时目标 CPU
        // 可能已经把线程选中甚至再次阻塞，不能据此误判失败。
        return task::enqueueThread(this);
    }

    /*
     * 设置线程允许运行的 CPU 集合。
     * 过滤无效 CPU，若当前 CPU 不再允许则安排迁移。
     */
    bool Thread::setAffinity(uint64_t cpuMask) {
        if (cpuMask == 0) {
            return false;
        }

        cpuAffinity = cpuMask;

        if ((cpuMask & (1ULL << cpuId)) != 0) {
            return true;
        }

        for (uint32_t i = 0; i < cpu::getDiscoveredCpuCount() && i < 64; ++i) {
            if ((cpuMask & (1ULL << i)) != 0 && migrate(i)) {
                return true;
            }
        }

        return false;
    }

    /*
     * 累计当前线程的真实和虚拟执行时间，判断是否用完本轮份额并需要重新调度。
     */
    bool RunQueue::accountTick(Thread* current, uint64_t elapsed) {
        RunQueue* queue = this;

        if (current != nullptr && current != queue->getIdleThread() &&
            current->getState() == ThreadState::Running) {
            current->setRuntime(current->getRuntime() + (elapsed));

            if (current->getTask()) {
                current->getTask()->accountCpu(elapsed);
            }

            current->setSliceRuntime(current->getSliceRuntime() + (elapsed));
            const uint64_t delta =
                calculateVruntimeDelta(elapsed, current->getWeight());

            // 按线程权重折算本轮执行时间，权重越大，同样真实时间增加的虚拟时间越少。
            current->setVruntime(current->getVruntime() + (delta));

            // 是否请求切换同时考虑时间片和队首候选，返回值只表达调度需求，不在时钟记账函数里直接换栈。
            if (current->getSliceRuntime() >= current->getTimeSlice() ||
                (queue->getLeftmost() != nullptr &&
                 queue->getLeftmost()->getVruntime() <
                     current->getVruntime())) {
                current->setSliceRuntime(0);

                return true;
            }
        }

        return false;
    }

}  // namespace task
