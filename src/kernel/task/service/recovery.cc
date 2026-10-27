/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/service/recovery.cc
 * Role-owned recovery snapshots, object escrow, and freeze deadlines.
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

namespace kernel::task {

namespace {
// One recovery domain per supervised role. Snapshot/log storage is bounded
// retained RAM, not power-loss persistence. Tokens belong to domain.objects.
constexpr uint32_t roles =
    static_cast<uint32_t>(os::ServiceRole::MessageQueue) + 1;
constexpr uint64_t maximumBytes       = 4ULL * 1024 * 1024;
constexpr uint32_t maximumObjects     = 16384;
constexpr uint32_t maximumRecords     = 128;
constexpr uint64_t maximumRecordBytes = 64 * 1024;
// Claim expired freezes once, then kill outside task-list/state locks.
// Retained iteration references keep each target alive through cleanup.
void watchdog(void*) {
    while (true) {
        sleepCurrentThread(250);
        uint64_t cursor = 1;
        while (true) {
            auto target = task::next(cursor);
            if (!target) break;
            cursor = target->id() + 1;
            if (!target->claimMemoryFreezeExpiry(time::getMonotonicTime()))
                continue;
            device::serialWrite(
                "[MM] recovery timeout; terminating frozen task ");
            device::serialWriteDecimal(target->id());
            device::serialWrite("\n");
            target->terminateOtherThreads(-9);
            target->destroy();
        }
    }
}
// Allocate role-owned pages that outlive the crashing process address space.
mm::PageObject* allocate(uint64_t bytes, uint32_t role) {
    uint32_t order = 0;
    while ((4096ULL << order) < bytes) ++order;
    void* storage = mm::allocateHeap(sizeof(mm::PageObject));
    if (!storage) return nullptr;
    auto* object = new (storage)
        mm::PageObject(order, 0x8000000000000000ULL | role, mm::anyMemoryNode);
    if (!object->valid()) {
        object->release();
        return nullptr;
    }
    return object;
}
}  // namespace

// Shared role domains, independent of a particular service instance.
ServiceRecovery& serviceRecovery() {
    static ServiceRecovery instance;
    return instance;
}
// Create the timeout worker before clients can be left frozen.
void ServiceRecovery::init() {
    if (!Thread::create(nullptr, watchdog, nullptr)) {
        device::serialWrite("[RECOVERY] cannot create watchdog\n");
        cpu::halt();
    }
}

// Accept only an unowned domain or this exact role instance as current.
bool ServiceRecovery::current(const Task& caller) {
    const auto role = static_cast<uint32_t>(caller.serviceRole());
    if (!role || role >= roles) return false;
    InterruptSpinLockGuard guard(lock);
    return domains[role].ownedBy(caller.id());
}
// Authorize the managed caller, then serialize its role domain. args[0] is the
// acquired epoch; commits/drops also carry an expected sequence. Reject stale
// epochs after a replacement takes ownership.
int ServiceRecovery::call(Task& caller, const IpcMessage& request,
                          IpcMessage& reply) {
    const uint32_t role = static_cast<uint32_t>(caller.serviceRole());
    if (!caller.managedService() || !role || role >= roles ||
        request.length < 5)
        return -3;
    const auto op    = static_cast<os::TaskOperation>(request.type);
    const auto* args = request.data;
    PreemptionSpinLockGuard guard(lock);
    auto& domain = domains[role];
    reply.length = 5;
    // Wait for the old owner to die and finish resource reclamation before
    // changing epoch. Discard preparation but preserve committed snapshot/log.
    if (op == os::TaskOperation::RecoveryAcquire) {
        if (domain.owner != caller.id()) {
            auto previous = task::find(domain.owner);
            if (domain.owner && previous && !previous->isTerminated())
                return -8;
            if (previous && !previous->resourcesReclaimed())
                return static_cast<int>(os::ErrorCode::WouldBlock);
            if (!domain.initialized) {
                if (!domain.objects.init(64, nullptr, true)) return -5;
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
        // Return a read-only reference without transferring the domain's
        // ownership. Undo only the new reference if caller-table installation
        // fails.
        if (domain.committed) {
            domain.committed->retain();
            const auto handle = caller.handles().add(
                domain.committed, HandleType::PageObject, handleRightRead);
            if (!handle) {
                domain.committed->release();
                return -5;
            }
            addCreatedHandle(reply, handle);
        }
        return 0;
    }
    if (domain.owner != caller.id() || !args[0] || args[0] != domain.epoch)
        return -3;
    // Check that a recovery record still names this process's exec generation.
    if (op == os::TaskOperation::RecoveryGeneration) {
        auto target = task::find(args[1]);
        if (!target || target->isTerminated())
            return static_cast<int>(os::ErrorCode::NotFound);
        PreemptionSpinLockGuard address(target->addressSpace().lock());
        if (!target->addressSpace().rootPhysicalAddress())
            return static_cast<int>(os::ErrorCode::NotFound);
        reply.data[1] = target->addressSpace().generation();
        return 0;
    }
    // The caller receives one read-only log reference; the domain retains its
    // own.
    if (op == os::TaskOperation::RecoveryReadLog) {
        if (args[1] >= domain.recordCount)
            return static_cast<int>(os::ErrorCode::NotFound);
        auto& record = domain.records[args[1]];
        record.data->retain();
        const auto handle = caller.handles().add(
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
    // Reserve without publishing. Append needs an existing snapshot and checks
    // record count plus allocated bytes, not only logical payload length.
    if (op == os::TaskOperation::RecoveryPrepare) {
        if (!args[1] || args[1] > maximumBytes || args[2] > 1) return -1;
        const bool append        = args[2] == 1;
        uint64_t allocationBytes = 4096;
        while (allocationBytes < args[1]) allocationBytes *= 2;
        if (append) {
            if (!domain.committed || args[1] > maximumRecordBytes) return -1;
            if (domain.recordCount == maximumRecords ||
                allocationBytes > maximumBytes - domain.logBytes)
                return static_cast<int>(os::ErrorCode::WouldBlock);
        }
        if (domain.prepared &&
            (domain.preparedLog != append ||
             (append && domain.prepared->size() != allocationBytes))) {
            domain.prepared->release();
            domain.prepared      = nullptr;
            domain.preparedBytes = 0;
        }
        // Reuse spare only at one reference; other readers must never observe
        // a previously published snapshot being overwritten for the next
        // commit.
        if (!domain.prepared || domain.prepared->size() < args[1]) {
            auto* prepared = !append && domain.spare &&
                                     domain.spare->size() >= args[1] &&
                                     domain.spare->references() == 1
                                 ? domain.spare
                                 : allocate(args[1], role);
            if (!prepared) return -5;
            if (prepared == domain.spare) domain.spare = nullptr;
            if (domain.prepared) domain.prepared->release();
            domain.prepared = prepared;
        }
        domain.preparedBytes = args[1];
        domain.preparedLog   = append;
        return 0;
    }
    // Validate mode/size/sequence, then pin the source before copying. Only
    // the private domain buffer becomes committed; caller memory is not
    // retained.
    if (op == os::TaskOperation::RecoveryCommit ||
        op == os::TaskOperation::RecoveryAppend) {
        if (!domain.prepared || args[2] != domain.preparedBytes ||
            args[3] != domain.sequence)
            return -1;
        const bool append = op == os::TaskOperation::RecoveryAppend;
        if (append != domain.preparedLog) return -1;
        HandleTable::Pin pin(caller.handles(), args[1], HandleType::PageObject,
                             handleRightRead);
        auto* source = pin.as<mm::PageObject>();
        if (!source || source->size() < args[2]) return -2;
        const auto* input = reinterpret_cast<const uint8_t*>(
            0xffff800000000000ULL + source->physical());
        auto* output = reinterpret_cast<uint8_t*>(0xffff800000000000ULL +
                                                  domain.prepared->physical());
        for (uint64_t i = 0; i < args[2]; ++i) output[i] = input[i];
        // Transfer prepared ownership to the new record, charge its allocated
        // page size and advance sequence only when the record is published.
        if (append) {
            domain.records[domain.recordCount++] = {domain.prepared, args[2],
                                                    ++domain.sequence};
            domain.logBytes += domain.prepared->size();
            domain.prepared      = nullptr;
            domain.preparedBytes = 0;
            domain.preparedLog   = false;
            reply.data[1]        = domain.sequence;
            return 0;
        }
        // A full snapshot subsumes earlier logs. Keep the old buffer as spare
        // only when it has no external reader; otherwise release the domain
        // reference.
        auto* previous          = domain.committed;
        domain.committed        = domain.prepared;
        domain.prepared         = nullptr;
        domain.bytes            = args[2];
        domain.preparedBytes    = 0;
        reply.data[1]           = ++domain.sequence;
        domain.snapshotSequence = domain.sequence;
        for (uint32_t i = 0; i < domain.recordCount; ++i) {
            domain.records[i].data->release();
            domain.records[i].clear();
        }
        domain.recordCount = 0;
        domain.logBytes    = 0;
        if (previous) {
            if (previous->references() == 1) {
                if (domain.spare) domain.spare->release();
                domain.spare = previous;
            } else
                previous->release();
        }
        return 0;
    }
    // Deduplicate eligible objects by identity. An endpoint additionally needs
    // matching receiver/role protection before it may survive owner death.
    if (op == os::TaskOperation::RecoveryPut) {
        void* object    = nullptr;
        HandleType type = HandleType::None;
        uint32_t rights = 0;
        if (!caller.handles().inspect(args[1], &object, &type, &rights) ||
            (type != HandleType::PageObject &&
             type != HandleType::SharedMemory && type != HandleType::Endpoint))
            return -2;
        HandleTable::Pin pin(caller.handles(), args[1], type, 0);
        if (pin.as<void>() != object) return -2;
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
        if (domain.objectCount == maximumObjects) return -5;
        if (type == HandleType::Endpoint &&
            ((rights & (handleRightRead | handleRightManage)) !=
                 (handleRightRead | handleRightManage) ||
             !static_cast<Endpoint*>(object)->protectRecovery(caller)))
            return -3;
        // Escrow owns a separate reference. Roll back endpoint protection if
        // no retained token was installed; there is then nothing for recovery
        // to adopt.
        const auto token = caller.handles().copyTo(domain.objects, args[1]);
        if (!token) {
            if (type == HandleType::Endpoint)
                static_cast<Endpoint*>(object)->releaseRecovery(caller);
            return -5;
        }
        ++domain.objectCount;
        reply.data[1] = token;
        return 0;
    }
    // Translate a domain token into a caller-local handle. A protected
    // endpoint also adopts the new receiver without changing its stable object
    // identity.
    if (op == os::TaskOperation::RecoveryGet) {
        void* object    = nullptr;
        HandleType type = HandleType::None;
        uint32_t rights = 0;
        if (!domain.objects.inspect(args[1], &object, &type, &rights))
            return -2;
        if (type == HandleType::Endpoint &&
            !static_cast<Endpoint*>(object)->adoptRecovery(caller))
            return -3;
        const auto handle = domain.objects.copyTo(caller.handles(), args[1]);
        if (!handle) return -5;
        reply.data[1] = handle;
        addCreatedHandle(reply, handle);
        return 0;
    }
    // Require sequence agreement before discarding an escrow reference needed
    // by the currently committed recovery state.
    if (op == os::TaskOperation::RecoveryDrop) {
        if (args[2] != domain.sequence) return -1;
        void* object    = nullptr;
        HandleType type = HandleType::None;
        uint32_t rights = 0;
        if (!domain.objects.inspect(args[1], &object, &type, &rights))
            return -2;
        if (type == HandleType::Endpoint)
            static_cast<Endpoint*>(object)->releaseRecovery(caller);
        if (!domain.objects.close(args[1])) return -2;
        --domain.objectCount;
        return 0;
    }
    // Return token and next-slot cursor separately; their encodings differ.
    if (op == os::TaskOperation::RecoveryList) {
        if (args[1] > UINT32_MAX) return -1;
        for (uint32_t i = args[1]; i < domain.objects.capacity(); ++i) {
            uint64_t token;
            void* object;
            HandleType type;
            uint32_t rights;
            if (!domain.objects.inspectAt(i, &token, &object, &type, &rights))
                continue;
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

// Stop ordinary noncurrent threads, then quiesce outside lock_. Thaw must
// respect an independent job-control stop and restore prior wait states.
bool Task::setMemoryFrozen(bool frozen) {
    if (managedService_ || this == task::getIdle() ||
        this == task::getCurrent())
        return false;
    {
        InterruptSpinLockGuard guard(lock_);
        if (state_ == TaskState::Terminated) return false;
        // Start one twenty-second budget, without extending repeated freeze
        // calls.
        if (frozen && !memoryFrozen_)
            memoryFreezeDeadline_ = time::getMonotonicTime() + 20000000000ULL;
        if (!frozen) memoryFreezeDeadline_ = 0;
        __atomic_store_n(&memoryFrozen_, frozen, __ATOMIC_RELEASE);
        for (Thread* thread = threads_; thread;
             thread         = thread->getTaskNext()) {
            if (frozen) {
                if (thread->getState() == ThreadState::Stopped ||
                    thread->getState() == ThreadState::Terminated)
                    continue;
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
    if (frozen) task::quiesceTask(this);
    return true;
}

// Claim the timeout under lock_; perform slow termination in the watchdog.
bool Task::claimMemoryFreezeExpiry(uint64_t now) {
    InterruptSpinLockGuard guard(lock_);
    if (!memoryFrozen_ || !memoryFreezeDeadline_ ||
        now < memoryFreezeDeadline_ || state_ == TaskState::Terminated)
        return false;
    memoryFreezeDeadline_ = 0;
    return true;
}

}  // namespace kernel::task
