#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"

namespace {

constexpr uint64_t pageSize           = 4096;
constexpr uint32_t objectCapacity     = 16;
constexpr uint32_t handleCapacity     = 64;
constexpr uint32_t mappingCapacity    = 64;
constexpr uint32_t pageCapacity       = 64;
constexpr uint32_t nameCapacity       = 64;
constexpr uint64_t minimumUserAddress = 0x10000;
constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
constexpr int openAccessMode          = 3;
constexpr int openReadOnly            = 0;
constexpr int openReadWrite           = 2;
constexpr int openCreate              = 0x40;
constexpr int openExclusive           = 0x80;

struct SharedMemoryObject {
    bool used;
    bool linked;
    char name[nameCapacity];
    uint32_t references;
    uint32_t mappings;
    uint32_t pageCount;
    uint64_t size;
    uint64_t pages[pageCapacity];
};

struct SharedMemoryHandle {
    bool used;
    SharedMemoryObject* object;
};

struct SharedMemoryMapping {
    bool used;
    kernel::Task* task;
    SharedMemoryObject* object;
    uint64_t address;
    uint64_t size;
    bool writable;
    bool pagerManaged = false;
};

SharedMemoryObject objects[objectCapacity];
SharedMemoryHandle handles[handleCapacity];
SharedMemoryMapping mappings[mappingCapacity];
kernel::SpinLock registryLock;

bool namesEqual(const char* left, const char* right) {
    uint32_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

void copyName(char* destination, const char* source) {
    uint32_t index = 0;
    while (index + 1 < nameCapacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void releaseObject(SharedMemoryObject& object) {
    if (object.linked || object.references != 0 || object.mappings != 0) return;
    for (uint32_t index = 0; index < object.pageCount; ++index) {
        if (object.pages[index] != 0)
            kernel::memory::freePhysicalPage(object.pages[index]);
        object.pages[index] = 0;
    }
    object.used      = false;
    object.name[0]   = '\0';
    object.pageCount = 0;
    object.size      = 0;
}

SharedMemoryHandle* resolve(kernel::Task& task, uint64_t handle,
                            uint32_t rights) {
    return static_cast<SharedMemoryHandle*>(
        task.handles().get(handle, kernel::HandleType::SharedMemory, rights));
}

}  // namespace

namespace kernel {

uint64_t SharedMemory::open(Task& task, const char* name, int flags) {
    const int access     = flags & openAccessMode;
    const bool anonymous = name == nullptr;
    if ((!anonymous && (name[0] != '/' || name[1] == '\0')) ||
        (anonymous && !(flags & openCreate)) ||
        (access != openReadOnly && access != openReadWrite))
        return 0;
    PreemptionSpinLockGuard guard(registryLock);
    SharedMemoryObject* object = nullptr;
    for (SharedMemoryObject& candidate : objects) {
        if (!anonymous && candidate.used && candidate.linked &&
            namesEqual(candidate.name, name)) {
            object = &candidate;
            break;
        }
    }
    if (object != nullptr && (flags & openCreate) != 0 &&
        (flags & openExclusive) != 0)
        return 0;
    if (object == nullptr) {
        if ((flags & openCreate) == 0) return 0;
        for (SharedMemoryObject& candidate : objects) {
            if (!candidate.used) {
                object             = &candidate;
                object->used       = true;
                object->linked     = !anonymous;
                object->references = 0;
                object->mappings   = 0;
                object->pageCount  = 0;
                object->size       = 0;
                if (anonymous)
                    object->name[0] = 0;
                else
                    copyName(object->name, name);
                break;
            }
        }
    }
    if (object == nullptr) return 0;
    SharedMemoryHandle* sharedHandle = nullptr;
    for (SharedMemoryHandle& candidate : handles) {
        if (!candidate.used) {
            sharedHandle     = &candidate;
            candidate.used   = true;
            candidate.object = object;
            break;
        }
    }
    if (sharedHandle == nullptr) {
        releaseObject(*object);
        return 0;
    }
    ++object->references;
    uint32_t rights = handleRightRead;
    if (access == openReadWrite) rights |= handleRightWrite | handleRightManage;
    const uint64_t result =
        task.handles().add(sharedHandle, HandleType::SharedMemory, rights);
    if (result == 0) {
        sharedHandle->used   = false;
        sharedHandle->object = nullptr;
        --object->references;
        releaseObject(*object);
    }
    return result;
}

uint64_t SharedMemory::duplicateHandle(Task& source, Task& destination,
                                       uint64_t handle,
                                       uint64_t destinationHandle) {
    return source.handles().copyTo(destination.handles(), handle,
                                   destinationHandle);
}

bool SharedMemory::unlink(const char* name) {
    if (name == nullptr) return false;
    PreemptionSpinLockGuard guard(registryLock);
    for (SharedMemoryObject& object : objects) {
        if (!object.used || !object.linked || !namesEqual(object.name, name)) {
            continue;
        }
        object.linked  = false;
        object.name[0] = '\0';
        releaseObject(object);
        return true;
    }
    return false;
}

bool SharedMemory::resize(Task& task, uint64_t handle, uint64_t size) {
    HandleTable::Pin pin(task.handles(), handle, HandleType::SharedMemory, 0);
    SharedMemoryHandle* sharedHandle = resolve(task, handle, handleRightManage);
    if (sharedHandle == nullptr || size > pageCapacity * pageSize) return false;
    SharedMemoryObject* object = sharedHandle->object;
    PreemptionSpinLockGuard registryGuard(registryLock);
    if (object->mappings != 0 && size < object->size) {
        return false;
    }
    const uint32_t pages =
        static_cast<uint32_t>((size + pageSize - 1) / pageSize);
    const uint32_t originalPageCount = object->pageCount;
    while (object->pageCount < pages) {
        const uint64_t physical = memory::allocatePhysicalPage();
        if (physical == 0) {
            while (object->pageCount > originalPageCount) {
                memory::freePhysicalPage(object->pages[--object->pageCount]);
                object->pages[object->pageCount] = 0;
            }
            return false;
        }
        uint8_t* bytes =
            reinterpret_cast<uint8_t*>(0xffff800000000000ULL + physical);
        for (uint64_t index = 0; index < pageSize; ++index) bytes[index] = 0;
        object->pages[object->pageCount++] = physical;
    }
    while (object->pageCount > pages) {
        memory::freePhysicalPage(object->pages[--object->pageCount]);
        object->pages[object->pageCount] = 0;
    }
    object->size = size;
    return true;
}

bool SharedMemory::querySize(Task& task, uint64_t handle, uint64_t& size) {
    HandleTable::Pin pin(task.handles(), handle, HandleType::SharedMemory, 0);
    auto* reference = pin.as<SharedMemoryHandle>();
    if (!reference) return false;
    PreemptionSpinLockGuard guard(registryLock);
    size = reference->object->size;
    return true;
}
bool SharedMemory::unlink(Task& task, uint64_t handle) {
    HandleTable::Pin pin(task.handles(), handle, HandleType::SharedMemory, 0);
    auto* reference = pin.as<SharedMemoryHandle>();
    if (!reference) return false;
    PreemptionSpinLockGuard guard(registryLock);
    auto& object = *reference->object;
    if (!object.linked) return false;
    object.linked  = false;
    object.name[0] = 0;
    releaseObject(object);
    return true;
}

uint64_t SharedMemory::map(Task& task, uint64_t handle, uint64_t address,
                           uint64_t size, uint64_t offset,
                           uint64_t protection) {
    HandleTable::Pin pin(task.handles(), handle, HandleType::SharedMemory, 0);
    const uint64_t end = address + size;
    const uint32_t requiredRights =
        handleRightRead | (((protection & 1) != 0) ? handleRightWrite : 0);
    SharedMemoryHandle* sharedHandle = resolve(task, handle, requiredRights);
    if (sharedHandle == nullptr || address == 0 || size == 0 ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        (offset & (pageSize - 1)) != 0 || (protection & ~3ULL) != 0 ||
        (protection & 3) == 3 || address < minimumUserAddress ||
        end <= address || end > maximumUserAddress)
        return 0;
    SharedMemoryObject* object = sharedHandle->object;
    PreemptionSpinLockGuard addressGuard(task.addressSpace().lock());
    PreemptionSpinLockGuard registryGuard(registryLock);
    const uint64_t mappedObjectSize =
        (object->size + pageSize - 1) & ~(pageSize - 1);
    if (object->size == 0 || offset >= object->size || offset + size < offset ||
        offset + size > mappedObjectSize)
        return 0;
    SharedMemoryMapping* mapping = nullptr;
    for (SharedMemoryMapping& candidate : mappings) {
        if (!candidate.used) {
            mapping = &candidate;
            break;
        }
    }
    if (mapping == nullptr) return 0;
    uint64_t flags = pageUser | pageExternal;
    if ((protection & 1) != 0) flags |= pageWritable;
    if ((protection & 2) == 0) flags |= pageNoExecute;
    uint64_t mapped = 0;
    while (mapped < size) {
        const uint32_t page =
            static_cast<uint32_t>((offset + mapped) / pageSize);
        if (!memory::mapPageInAddressSpace(
                task.addressSpace().rootPhysicalAddress(), address + mapped,
                object->pages[page], flags)) {
            while (mapped != 0) {
                mapped -= pageSize;
                memory::unmapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    address + mapped);
            }
            return 0;
        }
        mapped += pageSize;
    }
    mapping->used = true;
    mapping->pagerManaged =
        kernel::task::getCurrent() != &task &&
        kernel::task::getCurrent()->serviceRole() == os::ServiceRole::Memory;
    mapping->task     = &task;
    mapping->object   = object;
    mapping->address  = address;
    mapping->size     = size;
    mapping->writable = task.handles().get(handle, HandleType::SharedMemory,
                                           handleRightWrite) != nullptr;
    ++object->mappings;
    return address;
}

bool SharedMemory::prepareUnmap(Task& task, uint64_t address, uint64_t size,
                                void*& reservation) {
    reservation = nullptr;
    PreemptionSpinLockGuard guard(registryLock);
    for (const auto& mapping : mappings) {
        if (!mapping.used || mapping.task != &task ||
            address <= mapping.address ||
            address + size >= mapping.address + mapping.size)
            continue;
        for (auto& spare : mappings)
            if (!spare.used) {
                spare       = {};
                spare.used  = true;
                reservation = &spare;
                return true;
            }
        return false;
    }
    return true;
}
void SharedMemory::cancelUnmap(void* reservation) {
    if (!reservation) return;
    PreemptionSpinLockGuard guard(registryLock);
    *static_cast<SharedMemoryMapping*>(reservation) = {};
}
bool SharedMemory::releaseMapping(Task& task, uint64_t address, uint64_t size,
                                  void* reservation) {
    PreemptionSpinLockGuard guard(registryLock);
    auto* spare        = static_cast<SharedMemoryMapping*>(reservation);
    const uint64_t end = address + size;
    bool changed       = false;
    for (auto& mapping : mappings) {
        if (!mapping.used || mapping.task != &task) continue;
        const uint64_t oldEnd = mapping.address + mapping.size;
        if (end <= mapping.address || address >= oldEnd) continue;
        changed = true;
        if (address <= mapping.address && end >= oldEnd) {
            SharedMemoryObject* object = mapping.object;
            mapping                    = {};
            --object->mappings;
            releaseObject(*object);
        } else if (address > mapping.address && end < oldEnd) {
            if (!spare) return false;
            *spare         = mapping;
            spare->address = end;
            spare->size    = oldEnd - end;
            ++mapping.object->mappings;
            mapping.size = address - mapping.address;
            spare        = nullptr;
        } else if (address <= mapping.address) {
            mapping.address = end;
            mapping.size    = oldEnd - end;
        } else
            mapping.size = address - mapping.address;
    }
    if (spare) *spare = {};
    return changed;
}

bool SharedMemory::protectionAllowed(Task& task, uint64_t address,
                                     uint64_t size, uint64_t protection) {
    PreemptionSpinLockGuard guard(registryLock);
    const uint64_t end = address + size;
    if (end <= address) return false;
    for (const SharedMemoryMapping& mapping : mappings) {
        if (!mapping.used || mapping.task != &task) continue;
        const uint64_t mappingEnd = mapping.address + mapping.size;
        if (end <= mapping.address || address >= mappingEnd) continue;
        if ((protection & 1) != 0 && !mapping.writable) return false;
    }
    return true;
}

void SharedMemory::releaseTaskMappings(Task& task) {
    PreemptionSpinLockGuard guard(registryLock);
    for (SharedMemoryMapping& mapping : mappings) {
        if (!mapping.used || mapping.task != &task) continue;
        SharedMemoryObject* object = mapping.object;
        mapping                    = {};
        if (object->mappings != 0) --object->mappings;
        releaseObject(*object);
    }
}

bool SharedMemory::cloneTaskMappings(Task& source, Task& destination) {
    PreemptionSpinLockGuard guard(registryLock);
    uint32_t required  = 0;
    uint32_t available = 0;
    for (const SharedMemoryMapping& mapping : mappings) {
        if (mapping.used && mapping.task == &source) ++required;
        if (!mapping.used) ++available;
    }
    if (required > available) return false;
    for (const SharedMemoryMapping& sourceMapping : mappings) {
        if (!sourceMapping.used || sourceMapping.task != &source) continue;
        for (SharedMemoryMapping& destinationMapping : mappings) {
            if (destinationMapping.used) continue;
            destinationMapping      = sourceMapping;
            destinationMapping.task = &destination;
            ++destinationMapping.object->mappings;
            break;
        }
    }
    return true;
}

void SharedMemory::closeHandleObject(void* object) {
    auto* sharedHandle = static_cast<SharedMemoryHandle*>(object);
    if (sharedHandle == nullptr || !sharedHandle->used) return;
    PreemptionSpinLockGuard guard(registryLock);
    SharedMemoryObject* shared = sharedHandle->object;
    sharedHandle->used         = false;
    sharedHandle->object       = nullptr;
    if (shared != nullptr && shared->references != 0) --shared->references;
    if (shared != nullptr) releaseObject(*shared);
}
bool SharedMemory::nextManagedMapping(Task& task, uint64_t cursor,
                                      uint64_t& address, uint64_t& size) {
    PreemptionSpinLockGuard guard(registryLock);
    const SharedMemoryMapping* selected = nullptr;
    for (const auto& mapping : mappings)
        if (mapping.used && mapping.task == &task && mapping.pagerManaged &&
            mapping.address >= cursor &&
            (!selected || mapping.address < selected->address))
            selected = &mapping;
    if (!selected) return false;
    address = selected->address;
    size    = selected->size;
    return true;
}

}  // namespace kernel
