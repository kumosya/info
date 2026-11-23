/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/service/recovery.cc
 * 按服务角色保存检查点、代管对象，并处理冻结与恢复截止时间。
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"

namespace task {

    // 每个受监护角色有独立恢复域；快照和日志保存在有上限的内存中，不是断电持久化，令牌属于域自己的对象表。
    constexpr uint32_t roles =
        static_cast<uint32_t>(os::ServiceRole::MessageQueue) + 1;
    constexpr uint64_t maximumBytes       = 4ULL * 1024 * 1024;
    constexpr uint32_t maximumObjects     = 16384;
    constexpr uint32_t maximumRecords     = 128;
    constexpr uint64_t maximumRecordBytes = 64 * 1024;

    /*
     * 定期检查被冻结任务的截止时间。
     * 服务无法及时恢复时按策略结束冻结，避免进程无限期停止。
     */
    static void watchdog(void*) {
        while (true) {
            sleepCurrentThread(250);
            uint64_t cursor = 1;

            while (true) {
                ::task::TaskReference target = task::next(cursor);

                if (!target) {
                    break;
                }

                cursor = target->id() + 1;

                if (!target->claimMemoryFreezeExpiry(
                        time::getMonotonicTime())) {
                    continue;
                }

                device::serialWrite(
                    "[MM] recovery timeout; terminating frozen task ");
                device::serialWriteDecimal(target->id());
                device::serialWrite("\n");
                target->terminateOtherThreads(-9);
                target->destroy();
            }
        }
    }

    /*
     * 按恢复数据大小申请足够的页对象，并记录服务角色归属。
     * 页面大小按二次幂扩展，超过限制或分配失败时返回空指针。
     */
    static mm::PageObject* allocate(uint64_t bytes, uint32_t role) {
        uint32_t order = 0;

        while ((4096ULL << order) < bytes) {
            ++order;
        }

        void* storage = mm::allocateHeap(sizeof(mm::PageObject));

        if (!storage) {
            return nullptr;
        }

        mm::PageObject* object = new (storage) mm::PageObject(
            order, 0x8000000000000000ULL | role, mm::anyMemoryNode);

        if (!object->valid()) {
            object->release();

            return nullptr;
        }

        return object;
    }

    /*
     * 返回全局服务恢复管理器，统一维护各服务的检查点和代管对象。
     */
    ServiceRecovery& serviceRecovery() {
        static ServiceRecovery instance;

        return instance;
    }

    /*
     * 初始化恢复锁及后台看守线程，使恢复状态与超时处理在服务启动前可用。
     */
    void ServiceRecovery::init() {
        if (!Thread::create(nullptr, watchdog, nullptr)) {
            device::serialWrite("[RECOVERY] cannot create watchdog\n");
            cpu::halt();
        }
    }

    /*
     * 检查调用者是否为其服务角色当前登记的实例，拒绝旧实例继续修改恢复状态。
     */
    bool ServiceRecovery::current(const Task& caller) {
        const uint32_t role = static_cast<uint32_t>(caller.serviceRole());

        if (!role || role >= roles) {
            return false;
        }

        InterruptSpinLockGuard guard(lock);

        return domains[role].ownedBy(caller.id());
    }

    /*
     * 处理服务检查点、对象代管和重启接管请求。
     * 按服务身份和操作阶段验证后更新状态，准备中的数据与已提交的数据分别保存。
     */
    int ServiceRecovery::call(Task& caller, const IpcMessage& request,
                              IpcMessage& reply) {
        const uint32_t role = static_cast<uint32_t>(caller.serviceRole());

        if (!caller.managedService() || !role || role >= roles ||
            request.length < 5) {
            return -3;
        }

        const os::TaskOperation op =
            static_cast<os::TaskOperation>(request.type);
        const uint64_t* args = request.data;
        PreemptionSpinLockGuard guard(lock);
        Domain& domain = domains[role];
        reply.length   = 5;
        // 等待旧实例退出且资源回收完成再更换实例代号；丢弃准备中的数据，保留已经提交的快照和日志。

        // 取得恢复域时先确认旧实例已停止并回收；其他协议阶段还需核对当前拥有者和实例代号。
        if (op == os::TaskOperation::RecoveryAcquire) {
            if (domain.owner != caller.id()) {
                ::task::TaskReference previous = task::find(domain.owner);

                if (domain.owner && previous && !previous->isTerminated()) {
                    return -8;
                }

                if (previous && !previous->resourcesReclaimed()) {
                    return static_cast<int>(os::ErrorCode::WouldBlock);
                }

                if (!domain.initialized) {
                    if (!domain.objects.init(64, nullptr, true)) {
                        return -5;
                    }

                    domain.initialized = true;
                }

                if (domain.prepared) {
                    domain.prepared->release();
                    domain.prepared      = nullptr;
                    domain.preparedBytes = 0;
                }

                domain.owner = caller.id();
                ++domain.epoch;
            }

            reply.data[1] = domain.epoch;
            reply.data[2] = domain.sequence;
            reply.data[3] = domain.bytes;
            reply.data[4] = domain.snapshotSequence;
            reply.data[5] = domain.recordCount;
            reply.length  = 6;
            // 返回只读引用，恢复域继续拥有原引用；安装调用者句柄失败时只撤销新加的一份引用。
            if (domain.committed) {
                domain.committed->retain();
                const uint64_t handle = caller.handles().add(
                    domain.committed, HandleType::PageObject, handleRightRead);

                if (!handle) {
                    domain.committed->release();

                    return -5;
                }

                addCreatedHandle(reply, handle);
            }

            return 0;
        }

        if (domain.owner != caller.id() || !args[0] ||
            args[0] != domain.epoch) {
            return -3;
        }

        // 检查恢复记录是否仍对应目标进程当前的 exec 代号。
        if (op == os::TaskOperation::RecoveryGeneration) {
            ::task::TaskReference target = task::find(args[1]);

            if (!target || target->isTerminated()) {
                return static_cast<int>(os::ErrorCode::NotFound);
            }

            PreemptionSpinLockGuard address(target->addressSpace().lock());

            if (!target->addressSpace().rootPhysicalAddress()) {
                return static_cast<int>(os::ErrorCode::NotFound);
            }

            reply.data[1] = target->addressSpace().generation();

            return 0;
        }

        // 调用者取得一份只读日志引用，恢复域保留自己的原引用。
        if (op == os::TaskOperation::RecoveryReadLog) {
            if (args[1] >= domain.recordCount) {
                return static_cast<int>(os::ErrorCode::NotFound);
            }

            Domain::Record& record = domain.records[args[1]];
            record.data->retain();
            const uint64_t handle = caller.handles().add(
                record.data, HandleType::PageObject, handleRightRead);

            if (!handle) {
                record.data->release();

                return -5;
            }

            addCreatedHandle(reply, handle);
            reply.data[1] = record.bytes;
            reply.data[2] = record.sequence;

            return 0;
        }

        // 预留阶段不发布数据；追加日志需要已有快照，并同时检查记录数和实际分配空间，不能只看有效数据长度。

        // 预留阶段只准备缓冲区，不让读者看见新快照；提交操作才推进公开序号。
        if (op == os::TaskOperation::RecoveryPrepare) {
            if (!args[1] || args[1] > maximumBytes || args[2] > 1) {
                return -1;
            }

            const bool append        = args[2] == 1;
            uint64_t allocationBytes = 4096;

            while (allocationBytes < args[1]) {
                allocationBytes *= 2;
            }

            if (append) {
                if (!domain.committed || args[1] > maximumRecordBytes) {
                    return -1;
                }

                if (domain.recordCount == maximumRecords ||
                    allocationBytes > maximumBytes - domain.logBytes) {
                    return static_cast<int>(os::ErrorCode::WouldBlock);
                }
            }

            if (domain.prepared &&
                (domain.preparedLog != append ||
                 (append && domain.prepared->size() != allocationBytes))) {
                domain.prepared->release();
                domain.prepared      = nullptr;
                domain.preparedBytes = 0;
            }

            // 备用缓冲区只有一份引用时才能复用；还有读者时绝不能覆盖以前已发布的快照。
            if (!domain.prepared || domain.prepared->size() < args[1]) {
                mm::PageObject* prepared =
                    !append && domain.spare &&
                            domain.spare->size() >= args[1] &&
                            domain.spare->references() == 1
                        ? domain.spare
                        : allocate(args[1], role);

                if (!prepared) {
                    return -5;
                }

                if (prepared == domain.spare) {
                    domain.spare = nullptr;
                }

                if (domain.prepared) {
                    domain.prepared->release();
                }

                domain.prepared = prepared;
            }

            domain.preparedBytes = args[1];
            domain.preparedLog   = append;

            return 0;
        }

        // 先检查模式、大小和序号，再固定源对象并复制；提交的是域内私有缓冲区，不保留调用者的临时内存。
        if (op == os::TaskOperation::RecoveryCommit ||
            op == os::TaskOperation::RecoveryAppend) {
            if (!domain.prepared || args[2] != domain.preparedBytes ||
                args[3] != domain.sequence) {
                return -1;
            }

            const bool append = op == os::TaskOperation::RecoveryAppend;

            if (append != domain.preparedLog) {
                return -1;
            }

            HandleTable::Pin pin(caller.handles(), args[1],
                                 HandleType::PageObject, handleRightRead);
            mm::PageObject* source = pin.as<mm::PageObject>();

            if (!source || source->size() < args[2]) {
                return -2;
            }

            const uint8_t* input = reinterpret_cast<const uint8_t*>(
                0xffff800000000000ULL + source->physical());
            uint8_t* output = reinterpret_cast<uint8_t*>(
                0xffff800000000000ULL + domain.prepared->physical());

            for (uint64_t i = 0; i < args[2]; ++i) {
                output[i] = input[i];
            }

            // 准备好的所有权转给新日志记录，按实际页块大小记账，记录发布后才推进序号。
            if (append) {
                domain.records[domain.recordCount++] = {
                    domain.prepared, args[2], ++domain.sequence};
                domain.logBytes += domain.prepared->size();
                domain.prepared      = nullptr;
                domain.preparedBytes = 0;
                domain.preparedLog   = false;
                reply.data[1]        = domain.sequence;

                return 0;
            }

            // 完整快照包含此前日志内容；旧缓冲区没有外部读者才留作备用，否则只归还域自己的引用。
            mm::PageObject* previous = domain.committed;
            domain.committed         = domain.prepared;
            domain.prepared          = nullptr;
            domain.bytes             = args[2];
            domain.preparedBytes     = 0;
            reply.data[1]            = ++domain.sequence;
            domain.snapshotSequence  = domain.sequence;

            for (uint32_t i = 0; i < domain.recordCount; ++i) {
                domain.records[i].data->release();
                domain.records[i].clear();
            }

            domain.recordCount = 0;
            domain.logBytes    = 0;

            if (previous) {
                if (previous->references() == 1) {
                    if (domain.spare) {
                        domain.spare->release();
                    }

                    domain.spare = previous;
                } else {
                    previous->release();
                }
            }

            return 0;
        }

        // 按对象身份去重；端点还须匹配接收者和角色保护条件，才能在原服务退出后继续存在。
        if (op == os::TaskOperation::RecoveryPut) {
            void* object    = nullptr;
            HandleType type = HandleType::None;
            uint32_t rights = 0;

            if (!caller.handles().inspect(args[1], &object, &type, &rights) ||
                (type != HandleType::PageObject &&
                 type != HandleType::SharedMemory &&
                 type != HandleType::Endpoint)) {
                return -2;
            }

            HandleTable::Pin pin(caller.handles(), args[1], type, 0);

            if (pin.as<void>() != object) {
                return -2;
            }

            for (uint32_t i = 0; i < domain.objects.capacity(); ++i) {
                uint64_t token = 0;
                void* stored   = nullptr;
                HandleType storedType;
                uint32_t storedRights;

                if (domain.objects.inspectAt(i, &token, &stored, &storedType,
                                             &storedRights) &&
                    stored == object && storedType == type) {
                    reply.data[1] = token;

                    return 0;
                }
            }

            if (domain.objectCount == maximumObjects) {
                return -5;
            }

            if (type == HandleType::Endpoint &&
                ((rights & (handleRightRead | handleRightManage)) !=
                     (handleRightRead | handleRightManage) ||
                 !static_cast<Endpoint*>(object)->protectRecovery(caller))) {
                return -3;
            }

            // 代管表自己持有独立引用；令牌安装失败就撤销端点保护，因为没有留下可供新实例接管的引用。
            const uint64_t token =
                caller.handles().copyTo(domain.objects, args[1]);

            if (!token) {
                if (type == HandleType::Endpoint) {
                    static_cast<Endpoint*>(object)->releaseRecovery(caller);
                }

                return -5;
            }

            ++domain.objectCount;
            reply.data[1] = token;

            return 0;
        }

        // 将恢复域令牌转换成新实例本地句柄；受保护端点同时交接接收者，但其对象身份保持稳定。
        if (op == os::TaskOperation::RecoveryGet) {
            void* object    = nullptr;
            HandleType type = HandleType::None;
            uint32_t rights = 0;

            if (!domain.objects.inspect(args[1], &object, &type, &rights)) {
                return -2;
            }

            if (type == HandleType::Endpoint &&
                !static_cast<Endpoint*>(object)->adoptRecovery(caller)) {
                return -3;
            }

            const uint64_t handle =
                domain.objects.copyTo(caller.handles(), args[1]);

            if (!handle) {
                return -5;
            }

            reply.data[1] = handle;
            addCreatedHandle(reply, handle);

            return 0;
        }

        // 丢弃已提交状态仍需要的代管引用之前，必须核对提交序号。
        if (op == os::TaskOperation::RecoveryDrop) {
            if (args[2] != domain.sequence) {
                return -1;
            }

            void* object    = nullptr;
            HandleType type = HandleType::None;
            uint32_t rights = 0;

            if (!domain.objects.inspect(args[1], &object, &type, &rights)) {
                return -2;
            }

            if (type == HandleType::Endpoint) {
                static_cast<Endpoint*>(object)->releaseRecovery(caller);
            }

            if (!domain.objects.close(args[1])) {
                return -2;
            }

            --domain.objectCount;

            return 0;
        }

        // 令牌与下次枚举槽位分别返回，两者编码不同，不能互相替代。
        if (op == os::TaskOperation::RecoveryList) {
            if (args[1] > UINT32_MAX) {
                return -1;
            }

            for (uint32_t i = args[1]; i < domain.objects.capacity(); ++i) {
                uint64_t token;
                void* object;
                HandleType type;
                uint32_t rights;

                if (!domain.objects.inspectAt(i, &token, &object, &type,
                                              &rights)) {
                    continue;
                }

                reply.data[1] = token;
                reply.data[2] = i + 1;
                reply.data[3] = static_cast<uint64_t>(type);

                return 0;
            }

            reply.data[1] = 0;

            return 0;
        }

        return -1;
    }

    /*
     * 冻结或恢复任务的线程。
     * 内存恢复冻结与终端暂停分别记录，恢复时不能错误唤醒仍被另一种原因暂停的线程。
     */
    bool Task::setMemoryFrozen(bool frozen) {
        if (managedService_ || this == task::getIdle() ||
            this == task::getCurrent()) {
            return false;
        }

        {
            InterruptSpinLockGuard guard(lock_);

            if (state_ == TaskState::Terminated) {
                return false;
            }

            // 首次冻结开始一个二十秒预算，重复冻结不能无限延长截止时间。
            if (frozen && !memoryFrozen_) {
                memoryFreezeDeadline_ =
                    time::getMonotonicTime() + 20000000000ULL;
            }

            if (!frozen) {
                memoryFreezeDeadline_ = 0;
            }

            __atomic_store_n(&memoryFrozen_, frozen, __ATOMIC_RELEASE);

            // 逐线程记录冻结前状态并调整可运行资格，解除冻结时依据记录恢复，不能一律强制置为就绪。
            for (Thread* thread = threads_; thread;
                 thread         = thread->getTaskNext()) {
                if (frozen) {
                    if (thread->getState() == ThreadState::Stopped ||
                        thread->getState() == ThreadState::Terminated) {
                        continue;
                    }

                    thread->setStateBeforeStop(thread->getState());
                    task::dequeueThread(thread);
                    thread->setState(ThreadState::Stopped);
                    cpu::requestRemoteReschedule(
                        static_cast<uint32_t>(thread->getCpuId()));
                } else if (!stopped_ &&
                           thread->getState() == ThreadState::Stopped) {
                    thread->setState(thread->getStateBeforeStop());

                    if (thread->getState() == ThreadState::Ready ||
                        thread->getState() == ThreadState::Running) {
                        thread->setState(ThreadState::Ready);
                        task::enqueueThread(thread);
                    }
                }
            }
        }

        if (frozen) {
            task::quiesceTask(this);
        }

        return true;
    }

    /*
     * 原子认领一次已经到期的内存冻结，只有成功认领者负责后续超时处理，避免多个
     * CPU 重复处理同一任务。
     */
    bool Task::claimMemoryFreezeExpiry(uint64_t now) {
        InterruptSpinLockGuard guard(lock_);

        if (!memoryFrozen_ || !memoryFreezeDeadline_ ||
            now < memoryFreezeDeadline_ || state_ == TaskState::Terminated) {
            return false;
        }

        memoryFreezeDeadline_ = 0;

        return true;
    }

}  // namespace task
