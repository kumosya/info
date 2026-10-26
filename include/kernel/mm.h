#pragma once

#include <cstddef>
#include <cstdint>

#include "kernel/boot.h"
#include "kernel/page.h"

namespace kernel::memory {

void initPhy(const BootInfo* bootInfo);
uint64_t allocatePhysicalPage();
void freePhysicalPage(uint64_t physicalAddress);
uint64_t getTotalMemoryBytes();
uint64_t getFreeMemoryBytes();

}  // namespace kernel::memory

namespace kernel::heap {

void init();
void* kernelAllocate(size_t size);
void kernelFree(void* address);
uint64_t reclaimCaches();

}  // namespace kernel::heap

namespace kernel {

class Task;

class SharedMemory {
   public:
    static uint64_t open(Task& task, const char* name, int flags);
    static uint64_t duplicateHandle(Task& source, Task& destination,
                                    uint64_t handle,
                                    uint64_t destinationHandle = 0);
    static bool unlink(const char* name);
    static bool resize(Task& task, uint64_t handle, uint64_t size);
    static bool querySize(Task& task,uint64_t handle,uint64_t& size);
    static bool unlink(Task& task,uint64_t handle);
    static uint64_t map(Task& task, uint64_t handle, uint64_t address,
                        uint64_t size, uint64_t offset, uint64_t protection);
    static bool prepareUnmap(Task& task, uint64_t address, uint64_t size,
                             void*& reservation);
    static void cancelUnmap(void* reservation);
    static bool releaseMapping(Task& task, uint64_t address, uint64_t size,
                               void* reservation = nullptr);
    static bool protectionAllowed(Task& task, uint64_t address, uint64_t size,
                                  uint64_t protection);
    static void releaseTaskMappings(Task& task);
    static bool cloneTaskMappings(Task& source, Task& destination);
    static void closeHandleObject(void* object);
    static bool nextManagedMapping(Task& task, uint64_t cursor,
                                   uint64_t& address, uint64_t& size);
};

}  // namespace kernel

namespace kernel::memory {
class KernelMapping {
   public:
    static void* map(uint64_t physical, uint32_t order);
    static uint64_t physical(const void* address);
    static void clear(uint64_t physical, uint64_t bytes);
};
class ObjectCache {
   public:
    void init(size_t objectSize);
    void* allocate();
    bool release(void* object);
    uint64_t shrink();

   private:
    struct Slab;
    Slab* slabs_     = nullptr;
    uint32_t stride_ = 0;
    uint32_t empty_  = 0;
    SpinLock lock_;
    void unlink(Slab* slab);
};
class PerCpuObjectCache {
   public:
    void init(size_t objectSize);
    void* allocate();
    void release(void* object);
    uint64_t drain();
    static void enableCpuLocal();

   private:
    static constexpr uint32_t cpuCapacity      = 64;
    static constexpr uint32_t magazineCapacity = 8;
    struct alignas(64) Magazine {
        SpinLock lock;
        uint32_t count = 0;
        void* objects[magazineCapacity]{};
    } magazines_[cpuCapacity];
    ObjectCache cache_;
    static uint32_t currentCpu();
};
// Fixed-capacity, externally backed storage, with double-free detection.
class FixedPool {
   public:
    bool init(void* storage, size_t bytes, size_t objectSize);
    void* allocate();
    bool release(void* object);
    size_t available();

   private:
    void* storage_   = nullptr;
    uint32_t stride_ = 0, capacity_ = 0, free_ = 0, first_ = UINT32_MAX;
    uint64_t used_[64]{};
    SpinLock lock_;
};
}  // namespace kernel::memory

namespace kernel {
struct BootInfo;
}
namespace kernel::memory {
class MemoryTopology {
   public:
    static void init(const BootInfo* boot);
    static uint16_t nodeForAddress(uint64_t address);
    static uint16_t currentNode();
    static uint16_t nodeCount();
    static void enableCpuLocal();
};
}  // namespace kernel::memory

namespace kernel {
class Endpoint;
class Task;
}  // namespace kernel
namespace kernel::memory {
class PagerBridge {
   public:
    static bool bind(Task& owner, Endpoint* endpoint);
    static bool resolve(uint64_t address, uint64_t faultFlags,
                        int* failure = nullptr);
    static bool clone(Task& parent, Task& child);
    static bool ensureUserMemory(uint64_t address, uint64_t size,
                                 bool writable);
};
}  // namespace kernel::memory

namespace os {
struct IpcMessage;
}
namespace kernel::memory {
class MemoryService {
   public:
    int call(Task& caller, const os::IpcMessage& request,
             os::IpcMessage& reply);
    uint64_t map(uint64_t address, uint64_t size, uint64_t flags,
                 uint64_t handle, uint64_t offset, uint64_t target);
    uint64_t unmap(uint64_t address, uint64_t size, uint64_t target);
    uint64_t protect(uint64_t address, uint64_t size, uint64_t protection,
                     uint64_t target);
};
}  // namespace kernel::memory
