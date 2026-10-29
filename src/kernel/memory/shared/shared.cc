/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/shared/shared.cc
 * Named shared-memory objects, handle lifetime, and mapping records.
 */

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/sync.h"
#include "kernel/task.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"

namespace kernel::mm {

namespace {
constinit SharedMemory sharedRegistry;

// Bound object/open-wrapper/mapping/page arrays. Anonymous objects start
// unlinked; no name is required to share them through handles and fork.
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

// Compare validated registry names through their terminating byte.
bool namesEqual(const char* left, const char* right) {
    uint32_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

// Bound stored names to nameCapacity - 1 bytes and always terminate.
void copyName(char* destination, const char* source) {
    uint32_t index = 0;
    while (index + 1 < nameCapacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

}  // namespace
// Shared object registry independent of any process-local handle table.
SharedMemory& sharedMemory() { return sharedRegistry; }
// Caller holds registryLock. Reclaim pages only after name linkage, open
// wrappers and mappings all vanish. Last-handle close alone must not free RAM
// still mapped in another process.
void SharedMemory::releaseObject(SharedMemoryObject& object) {
    if (object.linked || object.references != 0 || object.mappings != 0) return;
    for (uint32_t index = 0; index < object.pageCount; ++index) {
        if (object.pages[index] != 0) {
            if(object.typedAllocation)typedUsed_[(object.pages[index]-typedBase_)/pageSize]=false;
            else freePhysicalPage(object.pages[index]);
        }
        object.pages[index] = 0;
    }
    object.typedPool = object.typedAllocation = false;
    object.typedPolicy = 0;
    object.used      = false;
    object.name[0]   = '\0';
    object.pageCount = 0;
    object.size      = 0;
}

// Borrow an authorized wrapper; pin its HandleTable reference before keeping
// this pointer across later work or registry locking.
SharedMemory::SharedMemoryHandle* SharedMemory::resolve(
    kernel::task::Task& task, uint64_t handle, uint32_t rights) {
    return static_cast<SharedMemoryHandle*>(task.handles().get(
        handle, kernel::task::HandleType::SharedMemory, rights));
}

// Resolve/create a bounded object, allocate an open wrapper, then install
// caller-local rights. Handle duplicates share that wrapper; references counts
// open wrappers, not every copied table slot. Zero means failure; linked names
// remain until unlink even if no process currently holds them open.
uint64_t SharedMemory::open(::kernel::task::Task& task, const char* name,
                            int flags) {
    const int access     = flags & openAccessMode;
    const bool anonymous = name == nullptr;
    if ((!anonymous && (name[0] != '/' || name[1] == '\0')) ||
        (anonymous && !(flags & openCreate)) ||
        (access != openReadOnly && access != openReadWrite))
        return 0;
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
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
            if (!candidate.isUsed()) {
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
        if (!candidate.isUsed()) {
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
    uint32_t rights = ::kernel::task::handleRightRead;
    if (access == openReadWrite)
        rights |= ::kernel::task::handleRightWrite |
                  ::kernel::task::handleRightManage;
    const uint64_t result = task.handles().add(
        sharedHandle, ::kernel::task::HandleType::SharedMemory, rights);
    // Undo this wrapper/ref on table installation failure. releaseObject still
    // preserves a linked named object or an object with surviving mapped
    // views.
    if (result == 0) {
        sharedHandle->used   = false;
        sharedHandle->object = nullptr;
        --object->references;
        releaseObject(*object);
    }
    return result;
}


// A reserved RAM extent backs the named typed pool. Allocation bits and object
// references are protected by the same registry lock as mapped-view lifetimes.
int64_t SharedMemory::openTyped(kernel::task::Task& task,int flags,unsigned policy) {
    if((flags&~(3|0x80000)) || ((flags&3)!=0 && (flags&3)!=2))return -1;
    if(policy!=1 && policy!=2)return -7;
    const uint64_t handle=open(task,nullptr,0x42);
    if(!handle)return -5;
    bool ready=false;
    {
        kernel::task::PreemptionSpinLockGuard guard(registryLock);
        if(!typedBase_)typedBase_=allocatePhysicalBlock(8);
        if(typedBase_) {
            auto* reference=resolve(task,handle,0);
            reference->object->typedPool=true;
            reference->object->typedPolicy=policy;
            ready=true;
        }
    }
    if(!ready){task.handles().close(handle);return -5;}
    if((flags&3)==0) {
        const uint64_t readonly=task.handles().duplicate(handle,1,0);
        task.handles().close(handle);
        return readonly?static_cast<int64_t>(readonly):-5;
    }
    return handle;
}
int SharedMemory::typedInfo(kernel::task::Task& task,uint64_t handle,uint64_t& available) {
    kernel::task::HandleTable::Pin pin(task.handles(),handle,kernel::task::HandleType::SharedMemory,1);
    auto* reference=pin.as<SharedMemoryHandle>();if(!reference)return -2;
    kernel::task::PreemptionSpinLockGuard guard(registryLock);
    if(!reference->object->typedPool)return -1;
    unsigned free=0,run=0,longest=0;
    for(bool used:typedUsed_) {
        if(used)run=0;
        else {++free;++run;if(run>longest)longest=run;}
    }
    available=(reference->object->typedPolicy==2?longest:free)*pageSize;
    return 0;
}
int64_t SharedMemory::allocateTyped(kernel::task::Task& task,uint64_t handle,uint64_t size) {
    kernel::task::HandleTable::Pin pin(task.handles(),handle,kernel::task::HandleType::SharedMemory,1);
    auto* source=pin.as<SharedMemoryHandle>();if(!source)return -2;
    if(!size || (size&4095) || size>pageCapacity*pageSize)return -1;
    const auto allocation=open(task,nullptr,0x42);if(!allocation)return -5;
    bool success=false;
    {
        kernel::task::PreemptionSpinLockGuard guard(registryLock);
        if(source->object->typedPool) {
            const unsigned count=size/pageSize;unsigned selected[pageCapacity]{},found=0;
            for(unsigned i=0;i<typedPageCount && found<count;++i) {
                if(typedUsed_[i]) {if(source->object->typedPolicy==2)found=0;}
                else selected[found++]=i;
            }
            if(found==count) {
                auto* object=resolve(task,allocation,0)->object;
                object->typedAllocation=true;object->size=size;object->pageCount=count;
                for(unsigned i=0;i<count;++i) {
                    typedUsed_[selected[i]]=true;
                    object->pages[i]=typedBase_+selected[i]*pageSize;
                    clearPhysicalPages(object->pages[i],pageSize);
                }
                success=true;
            }
        }
    }
    if(!success){task.handles().close(allocation);return source->object->typedPool?-5:-1;}
    if(!resolve(task,handle,kernel::task::handleRightWrite)) {
        const uint64_t readonly=task.handles().duplicate(allocation,1,0);
        task.handles().close(allocation);
        return readonly?static_cast<int64_t>(readonly):-5;
    }
    return allocation;
}

// Share the existing handle Reference, optionally preserving its numeric value
// for fork; this does not create another name or open wrapper.
uint64_t SharedMemory::duplicateHandle(::kernel::task::Task& source,
                                       ::kernel::task::Task& destination,
                                       uint64_t handle,
                                       uint64_t destinationHandle) {
    return source.handles().copyTo(destination.handles(), handle,
                                   destinationHandle);
}

// Remove lookup linkage, not existing handles or views. Backing survives until
// independent wrapper and mapping ownership also reaches zero.
bool SharedMemory::unlink(const char* name) {
    if (name == nullptr) return false;
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
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

// Pin manage authority and reject shrinking live mappings. Grow by zeroed
// pages; allocation failure rolls back only this attempt's suffix, preserving
// the old size and original backing. Capacity is bounded by pageCapacity.
bool SharedMemory::resize(::kernel::task::Task& task, uint64_t handle,
                          uint64_t size) {
    ::kernel::task::HandleTable::Pin pin(
        task.handles(), handle, ::kernel::task::HandleType::SharedMemory, 0);
    SharedMemoryHandle* sharedHandle =
        resolve(task, handle, ::kernel::task::handleRightManage);
    if (sharedHandle == nullptr || size > pageCapacity * pageSize) return false;
    SharedMemoryObject* object = sharedHandle->object;
    ::kernel::task::PreemptionSpinLockGuard registryGuard(registryLock);
    if(object->typedPool || object->typedAllocation)return false;
    if (object->mappings != 0 && size < object->size) {
        return false;
    }
    const uint32_t pages =
        static_cast<uint32_t>((size + pageSize - 1) / pageSize);
    const uint32_t originalPageCount = object->pageCount;
    while (object->pageCount < pages) {
        const uint64_t physical = mm::allocatePhysicalPage();
        if (physical == 0) {
            while (object->pageCount > originalPageCount) {
                mm::freePhysicalPage(object->pages[--object->pageCount]);
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
        mm::freePhysicalPage(object->pages[--object->pageCount]);
        object->pages[object->pageCount] = 0;
    }
    object->size = size;
    return true;
}

// Logical object byte size, which may end partway through its last page.
bool SharedMemory::querySize(::kernel::task::Task& task, uint64_t handle,
                             uint64_t& size) {
    ::kernel::task::HandleTable::Pin pin(
        task.handles(), handle, ::kernel::task::HandleType::SharedMemory, 0);
    auto* reference = pin.as<SharedMemoryHandle>();
    if (!reference) return false;
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    size = reference->object->size;
    return true;
}
// Unlink the pinned object, avoiding lookup of a potentially reused name.
// Existing open wrappers and mapped views retain their references.
bool SharedMemory::unlink(::kernel::task::Task& task, uint64_t handle) {
    ::kernel::task::HandleTable::Pin pin(
        task.handles(), handle, ::kernel::task::HandleType::SharedMemory, 0);
    auto* reference = pin.as<SharedMemoryHandle>();
    if (!reference) return false;
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    auto& object = *reference->object;
    if (!object.linked) return false;
    object.linked  = false;
    object.name[0] = 0;
    releaseObject(object);
    return true;
}

// Pin source authority, lock address space before registry, then validate
// object bounds and permissions. Install borrowed external leaves before
// publishing one view count. Failure unmaps its prefix without freeing the
// object-owned physical pages.
uint64_t SharedMemory::map(::kernel::task::Task& task, uint64_t handle,
                           uint64_t address, uint64_t size, uint64_t offset,
                           uint64_t protection) {
    ::kernel::task::HandleTable::Pin pin(
        task.handles(), handle, ::kernel::task::HandleType::SharedMemory, 0);
    const uint64_t end = address + size;
    const uint32_t requiredRights =
        ::kernel::task::handleRightRead |
        (((protection & 1) != 0) ? ::kernel::task::handleRightWrite : 0);
    SharedMemoryHandle* sharedHandle = resolve(task, handle, requiredRights);
    if (sharedHandle == nullptr || address == 0 || size == 0 ||
        (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
        (offset & (pageSize - 1)) != 0 || (protection & ~3ULL) != 0 ||
        (protection & 3) == 3 || address < minimumUserAddress ||
        end <= address || end > maximumUserAddress)
        return 0;
    SharedMemoryObject* object = sharedHandle->object;
    ::kernel::task::PreemptionSpinLockGuard addressGuard(
        task.addressSpace().lock());
    ::kernel::task::PreemptionSpinLockGuard registryGuard(registryLock);
    const uint64_t mappedObjectSize =
        (object->size + pageSize - 1) & ~(pageSize - 1);
    if (object->typedPool || object->size == 0 || offset >= object->size || offset + size < offset ||
        offset + size > mappedObjectSize)
        return 0;
    SharedMemoryMapping* mapping = nullptr;
    for (SharedMemoryMapping& candidate : mappings) {
        if (!candidate.isUsed()) {
            mapping = &candidate;
            break;
        }
    }
    if (mapping == nullptr) return 0;
    uint64_t flags = pageUser | pageExternal;
    auto residency=task.addressSpace().residency();
    const bool pager=kernel::task::getCurrent() && kernel::task::getCurrent()!=&task &&
        kernel::task::getCurrent()->serviceRole()==os::ServiceRole::Memory;
    if(!pager && task.addressSpace().lockFuture() &&
       (!residency.set(address,size,true) || residency.bytes>task.memlockLimit()))return 0;
    if ((protection & 1) != 0) flags |= pageWritable;
    if ((protection & 2) == 0) flags |= pageNoExecute;
    uint64_t mapped = 0;
    while (mapped < size) {
        const uint32_t page =
            static_cast<uint32_t>((offset + mapped) / pageSize);
        if (!mm::mapPageInAddressSpace(
                task.addressSpace().rootPhysicalAddress(), address + mapped,
                object->pages[page], flags | (residency.contains(address+mapped)?pageLocked:0))) {
            while (mapped != 0) {
                mapped -= pageSize;
                mm::unmapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(),
                    address + mapped);
            }
            return 0;
        }
        mapped += pageSize;
    }
    task.addressSpace().residency()=residency;
    mapping->used = true;
    mapping->pagerManaged =
        kernel::task::getCurrent() != &task &&
        kernel::task::getCurrent()->serviceRole() == os::ServiceRole::Memory;
    mapping->task    = &task;
    mapping->object  = object;
    mapping->address = address;
    mapping->size    = size;
    // Preserve original handle write authority separately from current PTE
    // flags; a read-only handle cannot gain write later through generic
    // protection.
    mapping->writable =
        task.handles().get(handle, ::kernel::task::HandleType::SharedMemory,
                           ::kernel::task::handleRightWrite) != nullptr;
    ++object->mappings;
    return address;
}

// A strict middle hole needs another view record. Reserve a spare before
// removing PTEs; no free slot means failure while all old mappings remain.
bool SharedMemory::prepareUnmap(::kernel::task::Task& task, uint64_t address,
                                uint64_t size, void*& reservation) {
    reservation = nullptr;
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    for (const auto& mapping : mappings) {
        if (!mapping.isUsed() || mapping.task != &task ||
            address <= mapping.address ||
            address + size >= mapping.address + mapping.size)
            continue;
        for (auto& spare : mappings)
            if (!spare.isUsed()) {
                spare       = {};
                spare.used  = true;
                reservation = &spare;
                return true;
            }
        return false;
    }
    return true;
}
// Return an unused split reservation after another registry cannot prepare.
// This releases metadata capacity, not a physical/user page mapping.
void SharedMemory::cancelUnmap(void* reservation) {
    if (!reservation) return;
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    *static_cast<SharedMemoryMapping*>(reservation) = {};
}
// After PTE removal, delete/trim/split view records. Full deletion drops one
// mapping count; a middle hole consumes the spare and adds a second surviving
// view count, while simple prefix/suffix trimming needs no new reference.
bool SharedMemory::releaseMapping(::kernel::task::Task& task, uint64_t address,
                                  uint64_t size, void* reservation) {
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    auto* spare        = static_cast<SharedMemoryMapping*>(reservation);
    const uint64_t end = address + size;
    bool changed       = false;
    for (auto& mapping : mappings) {
        if (!mapping.isUsed() || mapping.task != &task) continue;
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

// Check every overlapping view's original write ceiling. Existing writable PTE
// state alone is not authorization for a later permission increase.
bool SharedMemory::protectionAllowed(::kernel::task::Task& task,
                                     uint64_t address, uint64_t size,
                                     uint64_t protection) {
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    const uint64_t end = address + size;
    if (end <= address) return false;
    for (const SharedMemoryMapping& mapping : mappings) {
        if (!mapping.isUsed() || mapping.task != &task) continue;
        const uint64_t mappingEnd = mapping.address + mapping.size;
        if (end <= mapping.address || address >= mappingEnd) continue;
        if ((protection & 1) != 0 && !mapping.writable) return false;
    }
    return true;
}

// Remove views after target execution has stopped. Object-owned pages may
// still be linked or used by other tasks; releaseObject decides actual backing
// lifetime separately from the dying task's external PTE teardown.
void SharedMemory::releaseTaskMappings(::kernel::task::Task& task) {
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    for (SharedMemoryMapping& mapping : mappings) {
        if (!mapping.isUsed() || mapping.task != &task) continue;
        SharedMemoryObject* object = mapping.object;
        mapping                    = {};
        if (object->mappings != 0) --object->mappings;
        releaseObject(*object);
    }
}

// Check spare record capacity before cloning any view. Child PTEs share the
// same object pages, so each inherited view must add its own mapping count
// before the child can run independently of parent unmap/close.
bool SharedMemory::cloneTaskMappings(::kernel::task::Task& source,
                                     ::kernel::task::Task& destination) {
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    uint32_t required  = 0;
    uint32_t available = 0;
    for (const SharedMemoryMapping& mapping : mappings) {
        if (mapping.used && mapping.task == &source) ++required;
        if (!mapping.isUsed()) ++available;
    }
    if (required > available) return false;
    for (const SharedMemoryMapping& sourceMapping : mappings) {
        if (!sourceMapping.isUsed() || sourceMapping.task != &source) continue;
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

// Last shared handle Reference releases one open wrapper, not a whole view.
// Then apply the independent name/mapping lifetime checks before freeing RAM.
void SharedMemory::closeHandleObject(void* object) {
    auto* sharedHandle = static_cast<SharedMemoryHandle*>(object);
    if (sharedHandle == nullptr || !sharedHandle->used) return;
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
    SharedMemoryObject* shared = sharedHandle->object;
    sharedHandle->used         = false;
    sharedHandle->object       = nullptr;
    if (shared != nullptr && shared->references != 0) --shared->references;
    if (shared != nullptr) releaseObject(*shared);
}
// Select next MM-origin view by start address for frozen-client recovery.
// Self-created views are not automatically classified as pager-managed.
bool SharedMemory::nextManagedMapping(::kernel::task::Task& task,
                                      uint64_t cursor, uint64_t& address,
                                      uint64_t& size) {
    ::kernel::task::PreemptionSpinLockGuard guard(registryLock);
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

}  // namespace kernel::mm
