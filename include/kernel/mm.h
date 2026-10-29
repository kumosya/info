#pragma once

#include <cstddef>
#include <cstdint>

#include "kernel/boot.h"
#include "kernel/page.h"
#include <os/ipc.h>

namespace kernel::task {
class Task;
class Endpoint;
class SpinLock;
}  // namespace kernel::task
class BootInfo;

namespace kernel::mm {

void initPhy(const ::BootInfo* bootInfo);
uint64_t allocatePhysicalPage();
void freePhysicalPage(uint64_t physicalAddress);
uint64_t getTotalMemoryBytes();
uint64_t getFreeMemoryBytes();

void initHeap();
void* allocateHeap(size_t size);
void freeHeap(void* address);
uint64_t reclaimCaches();

class SharedMemory {
   public:
    uint64_t open(kernel::task::Task& task, const char* name, int flags);
    uint64_t duplicateHandle(kernel::task::Task& source,
                             kernel::task::Task& destination, uint64_t handle,
                             uint64_t destinationHandle = 0);
    bool unlink(const char* name);
    bool resize(kernel::task::Task& task, uint64_t handle, uint64_t size);
    int64_t openTyped(kernel::task::Task& task,int flags,unsigned policy);
    int64_t allocateTyped(kernel::task::Task& task,uint64_t handle,uint64_t size);
    int typedInfo(kernel::task::Task& task,uint64_t handle,uint64_t& available);
    bool querySize(kernel::task::Task& task, uint64_t handle, uint64_t& size);
    bool unlink(kernel::task::Task& task, uint64_t handle);
    uint64_t map(kernel::task::Task& task, uint64_t handle, uint64_t address,
                 uint64_t size, uint64_t offset, uint64_t protection);
    bool prepareUnmap(kernel::task::Task& task, uint64_t address, uint64_t size,
                      void*& reservation);
    void cancelUnmap(void* reservation);
    bool releaseMapping(kernel::task::Task& task, uint64_t address,
                        uint64_t size, void* reservation = nullptr);
    bool protectionAllowed(kernel::task::Task& task, uint64_t address,
                           uint64_t size, uint64_t protection);
    void releaseTaskMappings(kernel::task::Task& task);
    bool cloneTaskMappings(kernel::task::Task& source,
                           kernel::task::Task& destination);
    void closeHandleObject(void* object);
    bool nextManagedMapping(kernel::task::Task& task, uint64_t cursor,
                            uint64_t& address, uint64_t& size);

   private:
    static constexpr uint32_t objectCapacity = 16, handleCapacity = 64,
                              mappingCapacity = 64;
    static constexpr uint32_t pageCapacity = 64, nameCapacity = 64;
    static constexpr unsigned typedPageCount = 256;
    uint64_t typedBase_{};
    bool typedUsed_[typedPageCount]{};
    class SharedMemoryObject {
        friend class SharedMemory;

       public:
        bool isUsed() const { return used; }

       private:
        bool used = false;
        bool linked{};
        bool typedPool{},typedAllocation{};
        unsigned typedPolicy{};
        char name[nameCapacity]{};
        uint32_t references{};
        uint32_t mappings{};
        uint32_t pageCount{};
        uint64_t size{};
        uint64_t pages[pageCapacity]{};
    };

    class SharedMemoryHandle {
        friend class SharedMemory;

       public:
        bool isUsed() const { return used; }

       private:
        bool used = false;
        SharedMemoryObject* object{};
    };

    class SharedMemoryMapping {
        friend class SharedMemory;

       public:
        bool isUsed() const { return used; }

       private:
        bool used = false;
        kernel::task::Task* task{};
        SharedMemoryObject* object{};
        uint64_t address{};
        uint64_t size{};
        bool writable{};
        bool pagerManaged = false;
    };

    SharedMemoryObject objects[objectCapacity]{};
    SharedMemoryHandle handles[handleCapacity]{};
    SharedMemoryMapping mappings[mappingCapacity]{};
    kernel::task::SpinLock registryLock;
    void releaseObject(SharedMemoryObject& object);
    SharedMemoryHandle* resolve(kernel::task::Task& task, uint64_t handle,
                                uint32_t rights);
};
SharedMemory& sharedMemory();

void* mapKernelPages(uint64_t physical, uint32_t order);
uint64_t physicalFromKernelPointer(const void* address);
void clearPhysicalPages(uint64_t physical, uint64_t bytes);

class ObjectCache {
   public:
    void init(size_t objectSize);
    void* allocate();
    bool release(void* object);
    uint64_t shrink();

   private:
    class Slab;
    Slab* slabs_     = nullptr;
    uint32_t stride_ = 0;
    uint32_t empty_  = 0;
    kernel::task::SpinLock lock_;
    void unlink(Slab* slab);
};
void enableCpuObjectCaches();
class PerCpuObjectCache {
   public:
    void init(size_t objectSize);
    void* allocate();
    void release(void* object);
    uint64_t drain();

   private:
    static constexpr uint32_t cpuCapacity      = 64;
    static constexpr uint32_t magazineCapacity = 8;
    class alignas(64) Magazine {
        friend class PerCpuObjectCache;

       public:
        void* take() { return count ? objects[--count] : nullptr; }

       private:
        kernel::task::SpinLock lock;
        uint32_t count = 0;
        void* objects[magazineCapacity]{};
    } magazines_[cpuCapacity];
    ObjectCache cache_;
    uint32_t currentCpu();
};

class MemoryTopology {
   public:
    void init(const ::BootInfo* boot);
    uint16_t nodeForAddress(uint64_t address);
    uint16_t currentNode();
    uint16_t nodeCount();
    void enableCpuLocal();

   private:
    class Range {
        friend class MemoryTopology;

       public:
        Range() = default;
        Range(uint64_t start, uint64_t stop, uint16_t domain)
            : first(start), end(stop), node(domain) {}
        bool contains(uint64_t address) const {
            return address >= first && address < end;
        }

       private:
        uint64_t first = 0, end = 0;
        uint16_t node = 0;
    };
    class Cpu {
        friend class MemoryTopology;

       public:
        Cpu() = default;
        Cpu(uint32_t id, uint16_t domain) : apic(id), node(domain) {}
        bool matches(uint32_t id) const { return apic == id; }

       private:
        uint32_t apic = 0;
        uint16_t node = 0;
    };
    Range ranges[64]{};
    Cpu cpus[256]{};
    uint32_t domains[maximumMemoryNodes]{};
    uint16_t nodes      = 1;
    uint32_t rangeCount = 0, cpuCount = 0;
    bool cpuLocalReady = false;
    uint16_t nodeForDomain(uint32_t domain);
};
MemoryTopology& memoryTopology();

class PagerBridge {
   public:
    bool bind(kernel::task::Task& owner, kernel::task::Endpoint* endpoint);
    bool resolve(uint64_t address, uint64_t faultFlags, int* failure = nullptr);
    bool clone(kernel::task::Task& parent, kernel::task::Task& child);
    bool ensureUserMemory(uint64_t address, uint64_t size, bool writable);

   private:
    int exchange(os::IpcMessage& request, os::IpcMessage& reply,
                 bool optional = false);
    kernel::task::SpinLock pagerLock;
    kernel::task::Endpoint* pagerEndpoint = nullptr;
};
PagerBridge& pagerBridge();

class MemoryService {
   public:
    MemoryService(Maps& mappings, PagerBridge& pager)
        : mappings_(mappings), pager_(pager) {}
    int call(kernel::task::Task& caller, const os::IpcMessage& request,
             os::IpcMessage& reply);
    uint64_t map(uint64_t address, uint64_t size, uint64_t flags,
                 uint64_t handle, uint64_t offset, uint64_t target);
    uint64_t unmap(uint64_t address, uint64_t size, uint64_t target,bool preserveLocks=false);
    uint64_t protect(uint64_t address, uint64_t size, uint64_t protection,
                     uint64_t target);

   private:
    Maps& mappings_;
    PagerBridge& pager_;
};

}  // namespace kernel::mm
