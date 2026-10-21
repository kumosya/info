#include "kernel/recovery.h"
#include "kernel/task.h"
#include "kernel/page.h"
#include "kernel/mm.h"
#include "kernel/new.h"
#include "kernel/service_message.h"
#include "kernel/time.h"
#include "kernel/serial.h"
#include "os/core_service.h"

namespace kernel {
namespace {
constexpr uint32_t roles = static_cast<uint32_t>(os::ServiceRole::MessageQueue) + 1;
constexpr uint64_t maximumBytes = 4ULL * 1024 * 1024;
constexpr uint32_t maximumObjects = 16384;
constexpr uint32_t maximumRecords = 128;
constexpr uint64_t maximumRecordBytes = 64 * 1024;
struct Domain {
    uint64_t owner = 0, epoch = 0, sequence = 0, bytes = 0, preparedBytes = 0;
    memory::PageObject* committed = nullptr;
    memory::PageObject* prepared = nullptr;
    memory::PageObject* spare = nullptr;
    struct Record { memory::PageObject* data = nullptr; uint64_t bytes = 0, sequence = 0; } records[maximumRecords];
    uint64_t snapshotSequence = 0, logBytes = 0;
    uint32_t recordCount = 0;
    bool preparedLog = false;
    HandleTable objects;
    uint32_t objectCount = 0;
    bool initialized = false;
};
Domain domains[roles];
SpinLock lock;
void watchdog(void*) {
    for (;;) {
        Thread::sleepCurrent(250);
        uint64_t cursor = 1;
        for (;;) {
            auto target = task::next(cursor); if (!target) break;
            cursor = target->id() + 1;
            if (!target->claimMemoryFreezeExpiry(time::getMonotonicTime())) continue;
            serial::serialWrite("[MM] recovery timeout; terminating frozen task "); serial::serialWriteDecimal(target->id()); serial::serialWrite("\n");
            target->terminateOtherThreads(-9); target->destroy();
        }
    }
}
memory::PageObject* allocate(uint64_t bytes, uint32_t role) {
    uint32_t order = 0;
    while ((4096ULL << order) < bytes) ++order;
    void* storage = heap::kernelAllocate(sizeof(memory::PageObject));
    if (!storage) return nullptr;
    auto* object = new (storage) memory::PageObject(order, 0x8000000000000000ULL | role, memory::anyMemoryNode);
    if (!object->valid()) { object->release(); return nullptr; }
    return object;
}
}

void ServiceRecovery::initialize() {
    if (!Thread::create(nullptr, watchdog, nullptr)) { serial::serialWrite("[RECOVERY] cannot create watchdog\n"); cpu::halt(); }
}

bool ServiceRecovery::current(const Task& caller) {
    const auto role = static_cast<uint32_t>(caller.serviceRole());
    if (!role || role >= roles) return false;
    InterruptSpinLockGuard guard(lock);
    return !domains[role].owner || domains[role].owner == caller.id();
}
int ServiceRecovery::call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
    const uint32_t role = static_cast<uint32_t>(caller.serviceRole());
    if (!caller.managedService() || !role || role >= roles || request.length < 5) return -3;
    const auto op = static_cast<os::TaskOperation>(request.type);
    const auto* args = request.data;
    PreemptionSpinLockGuard guard(lock);
    auto& domain = domains[role];
    reply.length = 5;
    if (op == os::TaskOperation::RecoveryAcquire) {
        if (domain.owner != caller.id()) {
            auto previous = task::find(domain.owner);
            if (domain.owner && previous && !previous->isTerminated()) return -8;
            if (previous && !previous->resourcesReclaimed()) return static_cast<int>(os::ErrorCode::WouldBlock);
            if (!domain.initialized) {
                if (!domain.objects.init(64, nullptr, true)) return -5;
                domain.initialized = true;
            }
            if (domain.prepared) { domain.prepared->release(); domain.prepared = nullptr; domain.preparedBytes = 0; }
            domain.owner = caller.id(); ++domain.epoch;
        }
        reply.data[1] = domain.epoch; reply.data[2] = domain.sequence; reply.data[3] = domain.bytes;
        reply.data[4] = domain.snapshotSequence; reply.data[5] = domain.recordCount; reply.length = 6;
        if (domain.committed) {
            domain.committed->retain();
            const auto handle = caller.handles().add(domain.committed, HandleType::PageObject, handleRightRead);
            if (!handle) { domain.committed->release(); return -5; }
            MessageData::created(reply, handle);
        }
        return 0;
    }
    if (domain.owner != caller.id() || !args[0] || args[0] != domain.epoch) return -3;
    if (op == os::TaskOperation::RecoveryGeneration) {
        auto target = task::find(args[1]);
        if (!target || target->isTerminated()) return static_cast<int>(os::ErrorCode::NotFound);
        PreemptionSpinLockGuard address(target->addressSpace().lock());
        if (!target->addressSpace().rootPhysicalAddress()) return static_cast<int>(os::ErrorCode::NotFound);
        reply.data[1] = target->addressSpace().generation(); return 0;
    }
    if (op == os::TaskOperation::RecoveryReadLog) {
        if (args[1] >= domain.recordCount) return static_cast<int>(os::ErrorCode::NotFound);
        auto& record = domain.records[args[1]];
        record.data->retain();
        const auto handle = caller.handles().add(record.data, HandleType::PageObject, handleRightRead);
        if (!handle) { record.data->release(); return -5; }
        MessageData::created(reply, handle);
        reply.data[1] = record.bytes; reply.data[2] = record.sequence; return 0;
    }
    if (op == os::TaskOperation::RecoveryPrepare) {
        if (!args[1] || args[1] > maximumBytes || args[2] > 1) return -1;
        const bool append = args[2] == 1;
        uint64_t allocationBytes = 4096;
        while (allocationBytes < args[1]) allocationBytes *= 2;
        if (append) {
            if (!domain.committed || args[1] > maximumRecordBytes) return -1;
            if (domain.recordCount == maximumRecords || allocationBytes > maximumBytes - domain.logBytes)
                return static_cast<int>(os::ErrorCode::WouldBlock);
        }
        if (domain.prepared && (domain.preparedLog != append || (append && domain.prepared->size() != allocationBytes))) {
            domain.prepared->release(); domain.prepared = nullptr; domain.preparedBytes = 0;
        }
        if (!domain.prepared || domain.prepared->size() < args[1]) {
            auto* prepared = !append && domain.spare && domain.spare->size() >= args[1] && domain.spare->references() == 1 ? domain.spare : allocate(args[1], role);
            if (!prepared) return -5;
            if (prepared == domain.spare) domain.spare = nullptr;
            if (domain.prepared) domain.prepared->release();
            domain.prepared = prepared;
        }
        domain.preparedBytes = args[1]; domain.preparedLog = append; return 0;
    }
    if (op == os::TaskOperation::RecoveryCommit || op == os::TaskOperation::RecoveryAppend) {
        if (!domain.prepared || args[2] != domain.preparedBytes || args[3] != domain.sequence) return -1;
        const bool append = op == os::TaskOperation::RecoveryAppend;
        if (append != domain.preparedLog) return -1;
        HandleTable::Pin pin(caller.handles(), args[1], HandleType::PageObject, handleRightRead);
        auto* source = pin.as<memory::PageObject>();
        if (!source || source->size() < args[2]) return -2;
        const auto* input = reinterpret_cast<const uint8_t*>(0xffff800000000000ULL + source->physical());
        auto* output = reinterpret_cast<uint8_t*>(0xffff800000000000ULL + domain.prepared->physical());
        for (uint64_t i = 0; i < args[2]; ++i) output[i] = input[i];
        if (append) {
            domain.records[domain.recordCount++] = {domain.prepared, args[2], ++domain.sequence};
            domain.logBytes += domain.prepared->size();
            domain.prepared = nullptr; domain.preparedBytes = 0; domain.preparedLog = false;
            reply.data[1] = domain.sequence; return 0;
        }
        auto* previous = domain.committed;
        domain.committed = domain.prepared; domain.prepared = nullptr; domain.bytes = args[2]; domain.preparedBytes = 0;
        reply.data[1] = ++domain.sequence;
        domain.snapshotSequence = domain.sequence;
        for (uint32_t i = 0; i < domain.recordCount; ++i) {
            domain.records[i].data->release(); domain.records[i] = {};
        }
        domain.recordCount = 0; domain.logBytes = 0;
        if (previous) {
            if (previous->references() == 1) {
                if (domain.spare) domain.spare->release();
                domain.spare = previous;
            } else previous->release();
        }
        return 0;
    }
    if (op == os::TaskOperation::RecoveryPut) {
        void* object = nullptr; HandleType type = HandleType::None; uint32_t rights = 0;
        if (!caller.handles().inspect(args[1], &object, &type, &rights) ||
            (type != HandleType::PageObject && type != HandleType::SharedMemory && type != HandleType::Endpoint)) return -2;
        HandleTable::Pin pin(caller.handles(), args[1], type, 0);
        if (pin.as<void>() != object) return -2;
        for (uint32_t i = 0; i < domain.objects.capacity(); ++i) {
            uint64_t token = 0; void* stored = nullptr; HandleType storedType; uint32_t storedRights;
            if (domain.objects.inspectAt(i, &token, &stored, &storedType, &storedRights) && stored == object && storedType == type) {
                reply.data[1] = token; return 0;
            }
        }
        if (domain.objectCount == maximumObjects) return -5;
        if (type == HandleType::Endpoint && ((rights & (handleRightRead | handleRightManage)) != (handleRightRead | handleRightManage) ||
            !static_cast<Endpoint*>(object)->protectRecovery(caller))) return -3;
        const auto token = caller.handles().copyTo(domain.objects, args[1]);
        if (!token) {
            if (type == HandleType::Endpoint) static_cast<Endpoint*>(object)->releaseRecovery(caller);
            return -5;
        }
        ++domain.objectCount; reply.data[1] = token; return 0;
    }
    if (op == os::TaskOperation::RecoveryGet) {
        void* object = nullptr; HandleType type = HandleType::None; uint32_t rights = 0;
        if (!domain.objects.inspect(args[1], &object, &type, &rights)) return -2;
        if (type == HandleType::Endpoint && !static_cast<Endpoint*>(object)->adoptRecovery(caller)) return -3;
        const auto handle = domain.objects.copyTo(caller.handles(), args[1]);
        if (!handle) return -5;
        reply.data[1] = handle; MessageData::created(reply, handle); return 0;
    }
    if (op == os::TaskOperation::RecoveryDrop) {
        if (args[2] != domain.sequence) return -1;
        void* object = nullptr; HandleType type = HandleType::None; uint32_t rights = 0;
        if (!domain.objects.inspect(args[1], &object, &type, &rights)) return -2;
        if (type == HandleType::Endpoint) static_cast<Endpoint*>(object)->releaseRecovery(caller);
        if (!domain.objects.close(args[1])) return -2;
        --domain.objectCount; return 0;
    }
    if (op == os::TaskOperation::RecoveryList) {
        if (args[1] > UINT32_MAX) return -1;
        for (uint32_t i = args[1]; i < domain.objects.capacity(); ++i) {
            uint64_t token; void* object; HandleType type; uint32_t rights;
            if (!domain.objects.inspectAt(i, &token, &object, &type, &rights)) continue;
            reply.data[1] = token; reply.data[2] = i + 1; reply.data[3] = static_cast<uint64_t>(type); return 0;
        }
        reply.data[1] = 0; return 0;
    }
    return -1;
}
}  // namespace kernel
