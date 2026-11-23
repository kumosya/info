/*
 * 内存对象、地址映射和管理接口。
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "kernel/boot.h"
#include "kernel/page.h"
#include <os/ipc.h>

namespace task {
    class Task;
    class Endpoint;
    class SpinLock;
}  // namespace task
class BootInfo;

namespace mm {

    void initPhy(const ::BootInfo* bootInfo);
    uint64_t allocatePhysicalPage();
    bool freePhysicalPage(uint64_t physicalAddress);
    uint64_t getTotalMemoryBytes();
    uint64_t getFreeMemoryBytes();

    void initHeap();
    void* allocateHeap(size_t size);
    void freeHeap(void* address);
    uint64_t reclaimCaches();

    /* 维护共享内存名称、对象、句柄和映射，统一管理对象寿命。 */
    class SharedMemory {
        public:
            uint64_t open(task::Task& task, const char* name, int flags);
            uint64_t duplicateHandle(task::Task& source,
                                     task::Task& destination, uint64_t handle,
                                     uint64_t destinationHandle = 0);
            bool unlink(const char* name);
            bool resize(task::Task& task, uint64_t handle, uint64_t size);
            int64_t openTyped(task::Task& task, int flags, unsigned policy);
            int64_t allocateTyped(task::Task& task, uint64_t handle,
                                  uint64_t size);
            int typedInfo(task::Task& task, uint64_t handle,
                          uint64_t& available);
            bool querySize(task::Task& task, uint64_t handle, uint64_t& size);
            bool unlink(task::Task& task, uint64_t handle);
            uint64_t map(task::Task& task, uint64_t handle, uint64_t address,
                         uint64_t size, uint64_t offset, uint64_t protection);
            bool prepareUnmap(task::Task& task, uint64_t address, uint64_t size,
                              void*& reservation);
            void cancelUnmap(void* reservation);
            bool releaseMapping(task::Task& task, uint64_t address,
                                uint64_t size, void* reservation = nullptr);
            bool protectionAllowed(task::Task& task, uint64_t address,
                                   uint64_t size, uint64_t protection);
            void releaseTaskMappings(task::Task& task);
            bool cloneTaskMappings(task::Task& source, task::Task& destination);
            void closeHandleObject(void* object);
            bool nextManagedMapping(task::Task& task, uint64_t cursor,
                                    uint64_t& address, uint64_t& size);

        private:
            static constexpr uint32_t objectCapacity = 16, handleCapacity = 64,
                                      mappingCapacity = 64;
            static constexpr uint32_t pageCapacity = 64, nameCapacity = 64;
            static constexpr unsigned typedPageCount = 256;
            uint64_t typedBase_{};
            bool typedUsed_[typedPageCount]{};
            /* 保存共享内存的实际页面、大小和使用计数。 */
            class SharedMemoryObject {
                    friend class SharedMemory;

                public:
                    bool isUsed() const { return used; }

                private:
                    bool used = false;
                    bool linked{};
                    bool typedPool{}, typedAllocation{};
                    unsigned typedPolicy{};
                    char name[nameCapacity]{};
                    uint32_t references{};
                    uint32_t mappings{};
                    uint32_t pageCount{};
                    uint64_t size{};
                    uint64_t pages[pageCapacity]{};
            };

            /* 记录一个共享内存句柄对对象的引用及打开属性。 */
            class SharedMemoryHandle {
                    friend class SharedMemory;

                public:
                    bool isUsed() const { return used; }

                private:
                    bool used = false;
                    SharedMemoryObject* object{};
            };

            /* 登记共享对象在任务中的地址、范围、偏移和允许权限。 */
            class SharedMemoryMapping {
                    friend class SharedMemory;

                public:
                    bool isUsed() const { return used; }

                private:
                    bool used = false;
                    task::Task* task{};
                    SharedMemoryObject* object{};
                    uint64_t address{};
                    uint64_t size{};
                    bool writable{};
                    bool pagerManaged = false;
            };

            SharedMemoryObject objects[objectCapacity]{};
            SharedMemoryHandle handles[handleCapacity]{};
            SharedMemoryMapping mappings[mappingCapacity]{};
            task::SpinLock registryLock;
            void releaseObject(SharedMemoryObject& object);
            SharedMemoryHandle* resolve(task::Task& task, uint64_t handle,
                                        uint32_t rights);
    };
    SharedMemory& sharedMemory();

    void* mapKernelPages(uint64_t physical, uint32_t order);
    uint64_t physicalFromKernelPointer(const void* address);
    void clearPhysicalPages(uint64_t physical, uint64_t bytes);

    /* 按固定大小缓存小对象，用位图管理页面内的空槽。 */
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
            task::SpinLock lock_;
            void unlink(Slab* slab);
    };
    void enableCpuObjectCaches();
    /* 在每个 CPU 暂存少量空闲对象，减少访问公共缓存的竞争。 */
    class PerCpuObjectCache {
        public:
            void init(size_t objectSize);
            void* allocate();
            void release(void* object);
            uint64_t drain();

        private:
            static constexpr uint32_t cpuCapacity      = 64;
            static constexpr uint32_t magazineCapacity = 8;
            /* 每个 CPU 的短小对象暂存区，独立对齐以减少相邻 CPU
             * 修改同一缓存行。 */
            class alignas(64) Magazine {
                    friend class PerCpuObjectCache;

                public:
                    void* take() { return count ? objects[--count] : nullptr; }

                private:
                    task::SpinLock lock;
                    uint32_t count = 0;
                    void* objects[magazineCapacity]{};
            } magazines_[cpuCapacity];
            ObjectCache cache_;
            uint32_t currentCpu();
    };

    /* 记录物理内存区间与 CPU 所属节点，支持优先分配就近内存。 */
    class MemoryTopology {
        public:
            void init(const ::BootInfo* boot);
            uint16_t nodeForAddress(uint64_t address);
            uint16_t currentNode();
            uint16_t nodeCount();
            void enableCpuLocal();

        private:
            /* 保存一个起止地址区间，供所属管理对象查询、合并或拆分。 */
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
            /* 记录固件 CPU 标识与内存节点的对应关系。 */
            class Cpu {
                    friend class MemoryTopology;

                public:
                    Cpu() = default;
                    Cpu(uint32_t id, uint16_t domain)
                        : apic(id), node(domain) {}
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

    /* 连接内核缺页处理与用户态内存服务，负责请求、端点绑定和重试。 */
    class PagerBridge {
        public:
            bool bind(task::Task& owner, task::Endpoint* endpoint);
            bool resolve(uint64_t address, uint64_t faultFlags,
                         int* failure = nullptr);
            bool clone(task::Task& parent, task::Task& child);
            bool ensureUserMemory(uint64_t address, uint64_t size,
                                  bool writable);

        private:
            int exchange(os::IpcMessage& request, os::IpcMessage& reply,
                         bool optional = false);
            task::SpinLock pagerLock;
            task::Endpoint* pagerEndpoint = nullptr;
    };
    PagerBridge& pagerBridge();

    /* 分发内存内核请求，执行页对象、映射和锁页等需要特权的操作。 */
    class MemoryService {
        public:
            MemoryService(Maps& mappings, PagerBridge& pager)
                : mappings_(mappings), pager_(pager) {}
            int call(task::Task& caller, const os::IpcMessage& request,
                     os::IpcMessage& reply);
            uint64_t map(uint64_t address, uint64_t size, uint64_t flags,
                         uint64_t handle, uint64_t offset, uint64_t target);
            uint64_t unmap(uint64_t address, uint64_t size, uint64_t target,
                           bool preserveLocks = false);
            uint64_t protect(uint64_t address, uint64_t size,
                             uint64_t protection, uint64_t target);

        private:
            Maps& mappings_;
            PagerBridge& pager_;
    };

}  // namespace mm
