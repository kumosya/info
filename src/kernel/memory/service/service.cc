#include "kernel/core_service.h"
#include "kernel/mm.h"
#include "kernel/ipc.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/new.h"
#include "os/mm.h"
#include "os/core_service.h"

namespace kernel::memory {
namespace {
class RequestData {
   public:
    static bool name(const IpcMessage& request, uint32_t word, const char*& name) {
        if (request.length <= word) return false;
        name = reinterpret_cast<const char*>(&request.data[word]);
        for (uint32_t i = 0; i < 64 && i < (request.length - word) * 8; ++i) if (!name[i]) return i != 0;
        return false;
    }
    static void copy(void* destination, const void* source, size_t size) {
        auto* output = static_cast<uint8_t*>(destination); auto* input = static_cast<const uint8_t*>(source);
        for (size_t i = 0; i < size; ++i) output[i] = input[i];
    }
    static bool authorized(Task& caller, uint64_t target) { return !target || target == caller.id() || (caller.serviceRole() == os::ServiceRole::Memory && caller.hasCapability(taskCapabilityMemory)); }
};
}
int MemoryService::call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
    if (request.length < 5) return -1;
    const auto* args = request.data; const char* name = nullptr;
    switch (static_cast<os::MemoryOperation>(request.type)) {
        case os::MemoryOperation::BindPager: {
            HandleTable::Pin pin(caller.handles(), args[0], HandleType::Endpoint, handleRightRead);
            if (!PagerBridge::bind(caller, pin.as<Endpoint>())) return -3;
            break;
        }
        case os::MemoryOperation::PageState: {
            if (!RequestData::authorized(caller, args[0])) return -3;
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target) return -4;
            PreemptionSpinLockGuard guard(target->addressSpace().lock());
            const auto root = target->addressSpace().rootPhysicalAddress();
            uint64_t flags = 0;
            if (root && memory::getPhysicalAddressInAddressSpace(root, args[1], &flags)) reply.data[1] = flags & (pagePresent | pageWritable | pageUser | pageExternal | pageNoExecute | (1ULL << 5) | (1ULL << 6));
            break;
        }
        case os::MemoryOperation::RangeAvailable: {
            if (!RequestData::authorized(caller, args[0])) return -3;
            if (!args[2] || ((args[1] | args[2]) & 4095) || args[1] < 0x10000 || args[1] >= 0x0000800000000000ULL || args[2] > 0x0000800000000000ULL - args[1]) return -1;
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target || target->isTerminated()) return -4;
            PreemptionSpinLockGuard guard(target->addressSpace().lock());
            const auto root = target->addressSpace().rootPhysicalAddress();
            if (!root) return -4;
            reply.data[1] = 1;
            for (uint64_t offset = 0; offset < args[2]; offset += 4096)
                if (memory::getPhysicalAddressInAddressSpace(root, args[1] + offset)) { reply.data[1] = 0; break; }
            break;
        }
        case os::MemoryOperation::Open:
            if (!RequestData::name(request, 5, name) || args[0] > UINT32_MAX) return -1;
            reply.data[0] = SharedMemory::open(caller, name, args[0]);
            if (reply.data[0]) reply.handles[reply.handleCount++] = reply.data[0];
            break;
        case os::MemoryOperation::Unlink:
            if (!RequestData::name(request, 5, name)) return -1;
            reply.data[0] = SharedMemory::unlink(name) ? 0 : -1; break;
        case os::MemoryOperation::Resize:
            reply.data[0] = SharedMemory::resize(caller, args[0], args[1]) ? 0 : -1; break;
        case os::MemoryOperation::AllocatePages: {
            if (args[0] > 12 || args[1] > UINT16_MAX || !RequestData::authorized(caller, args[2])) return -3;
            if (args[1] != anyMemoryNode && args[1] >= MemoryTopology::nodeCount()) return -1;
            void* storage = heap::kernelAllocate(sizeof(PageObject));
            if (!storage) return -5;
            auto* object = new (storage) PageObject(args[0], args[2] ? args[2] : caller.id(), args[1]);
            if (!object->valid()) { object->release(); return -5; }
            const uint64_t handle = caller.handles().add(object, HandleType::PageObject, handleRightRead | handleRightWrite | handleRightManage);
            if (!handle) { object->release(); return -5; }
            reply.data[0] = handle; reply.handles[reply.handleCount++] = handle; break;
        }
        case os::MemoryOperation::PageInfo: {
            HandleTable::Pin pin(caller.handles(), args[0], HandleType::PageObject, handleRightRead);
            auto* object = pin.as<PageObject>(); if (!object) return -2;
            PhysicalPageInfo physical{}; queryPhysicalPage(object->physical(), physical);
            const os::PageObjectInfo info{object->owner(), object->size(), physical.references, physical.node, static_cast<uint16_t>(object->order())};
            RequestData::copy(&reply.data[1], &info, sizeof(info)); break;
        }
        case os::MemoryOperation::Usage: {
            if (!RequestData::authorized(caller, args[0])) return -3;
            auto target = task::find(args[0] ? args[0] : caller.id());
            if (!target || target->isTerminated()) return -4;
            uint64_t generation;
            { PreemptionSpinLockGuard guard(target->addressSpace().lock()); generation = target->addressSpace().generation(); }
            const os::MemoryUsage info{getTotalMemoryBytes(), getFreeMemoryBytes(), getOwnedMemoryBytes(target->id()), generation, MemoryTopology::nodeCount(), 0};
            RequestData::copy(&reply.data[1], &info, sizeof(info)); break;
        }
        case os::MemoryOperation::ReclaimCaches:
            if (!caller.hasCapability(taskCapabilityMemory)) return -3;
            reply.data[0] = heap::reclaimCaches(); break;
        default: return -1;
    }
    reply.length = os::ipcDataWordCount; return 0;
}
}  // namespace kernel::memory

namespace kernel {
namespace {
class ResidentMemoryService final : public EndpointService {
    memory::MemoryService implementation_;
   public:
    int call(Task& caller, const IpcMessage& request, IpcMessage& reply) override {
        return implementation_.call(caller, request, reply);
    }
};
}  // namespace
EndpointService& memoryKernelService() {
    static ResidentMemoryService service;
    return service;
}
}  // namespace kernel
