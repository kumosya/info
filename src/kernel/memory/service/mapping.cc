#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include <os/device.h>
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"

namespace kernel::memory {
namespace {
uint64_t error(SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
}
class MappingCaller {
   public:
    static bool manager(const Task& caller) {
        return caller.serviceRole() == os::ServiceRole::Memory &&
               caller.hasCapability(taskCapabilityMemory) &&
               ServiceRecovery::current(caller);
    }
    static bool authorized(const Task& caller, uint64_t target) {
        return !target || target == caller.id() || manager(caller);
    }
};
}  // namespace
uint64_t MemoryService::map(uint64_t address, uint64_t size, uint64_t flags,
                            uint64_t handle, uint64_t offset,
                            uint64_t targetId) {
    auto* caller = task::getCurrent();
    if (!caller || !MappingCaller::authorized(*caller, targetId))
        return error(SyscallError::PermissionDenied);
    auto target = task::find(targetId ? targetId : caller->id());
    if (!target || target->isTerminated()) return error(SyscallError::NotFound);
    const uint64_t protection = flags & 3;
    switch (flags & ~3ULL) {
        case OS_MAP_ANONYMOUS:
            if (handle || offset) return error(SyscallError::InvalidArgument);
            return memory::MappingOperations::map(*target, address, size,
                                                  protection);
        case OS_MAP_PAGE_OBJECT: {
            if (protection == 3) return error(SyscallError::PermissionDenied);
            const uint32_t required =
                handleRightRead | ((protection & 1) ? handleRightWrite : 0);
            HandleTable::Pin pin(caller->handles(), handle,
                                 HandleType::PageObject, required);
            auto* object = pin.as<memory::PageObject>();
            if (!object) return error(SyscallError::InvalidHandle);
            void* inspected = nullptr;
            HandleType type = HandleType::None;
            uint32_t rights = 0;
            if (!caller->handles().inspect(handle, &inspected, &type,
                                           &rights) ||
                inspected != object || type != HandleType::PageObject)
                return error(SyscallError::InvalidHandle);
            const uint64_t maximum = 2 | ((rights & handleRightWrite) ? 1 : 0);
            return memory::PageMappings::map(*target, *object, address, size,
                                             offset, protection, maximum)
                       ? address
                       : error(SyscallError::InvalidArgument);
        }
        case OS_MAP_SHARED_OBJECT: {
            if (target == caller)
                return SharedMemory::map(*caller, handle, address, size, offset,
                                         protection);
            const uint64_t copied =
                SharedMemory::duplicateHandle(*caller, *target, handle);
            if (!copied) return error(SyscallError::InvalidHandle);
            const uint64_t result = SharedMemory::map(*target, copied, address,
                                                      size, offset, protection);
            target->handles().close(copied);
            return result;
        }
        default:
            return error(SyscallError::InvalidArgument);
    }
}
uint64_t MemoryService::unmap(uint64_t address, uint64_t size,
                              uint64_t targetId) {
    auto* caller = task::getCurrent();
    if (!caller || !MappingCaller::authorized(*caller, targetId))
        return error(SyscallError::PermissionDenied);
    auto target = task::find(targetId ? targetId : caller->id());
    return !target || target->isTerminated()
               ? error(SyscallError::NotFound)
               : memory::MappingOperations::unmap(*target, address, size);
}
uint64_t MemoryService::protect(uint64_t address, uint64_t size,
                                uint64_t protection, uint64_t targetId) {
    auto* caller = task::getCurrent();
    if (!caller || !MappingCaller::authorized(*caller, targetId))
        return error(SyscallError::PermissionDenied);
    auto target = task::find(targetId ? targetId : caller->id());
    if (!target || target->isTerminated()) return error(SyscallError::NotFound);
    if (protection == OS_PROTECT_COPY_ON_WRITE) {
        if (!MappingCaller::manager(*caller))
            return error(SyscallError::PermissionDenied);
        PreemptionSpinLockGuard guard(target->addressSpace().lock());
        return memory::PageMappings::markCopyOnWrite(*target, address, size)
                   ? 0
                   : error(SyscallError::InvalidArgument);
    }
    return memory::MappingOperations::protect(*target, address, size,
                                              protection);
}
}  // namespace kernel::memory
