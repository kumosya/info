#pragma once

#include <cstddef>
#include <cstdint>

#include "kernel/sync.h"

namespace kernel::task {
class Task;
class SpinLock;
}  // namespace kernel::task
class BootInfo;

namespace kernel::mm {

constexpr uint64_t pagePresent      = 1ULL << 0;
constexpr uint64_t pageWritable     = 1ULL << 1;
constexpr uint64_t pageUser         = 1ULL << 2;
constexpr uint64_t pageWriteThrough = 1ULL << 3;
constexpr uint64_t pageCacheDisable = 1ULL << 4;
constexpr uint64_t pageGlobal       = 1ULL << 8;
constexpr uint64_t pageExternal     = 1ULL << 9;
constexpr uint64_t pageLocked       = 1ULL << 10;
constexpr uint64_t pageNoExecute    = 1ULL << 63;

enum class UserPageMapResult : uint8_t {
    Success,
    InvalidArgument,
    OutOfMemory,
    MappingFailed
};

void initVir(const ::BootInfo* bootInfo);
bool mapPage(uint64_t virtualAddress, uint64_t physicalAddress, uint64_t flags);
bool unmapPage(uint64_t virtualAddress);
uint64_t getPhysicalAddress(uint64_t virtualAddress);
bool mapPageInAddressSpace(uint64_t rootPhysicalAddress,
                           uint64_t virtualAddress, uint64_t physicalAddress,
                           uint64_t flags);
bool unmapPageInAddressSpace(uint64_t rootPhysicalAddress,
                             uint64_t virtualAddress);
bool protectPageInAddressSpace(uint64_t rootPhysicalAddress,
                               uint64_t virtualAddress, uint64_t flags);
// Bounded batches complete all remote invalidations before returning. The
// caller holds the target address-space lock and owns backing-page release.
bool unmapPagesInAddressSpace(uint64_t root, uint64_t address, uint64_t size);
bool protectPagesInAddressSpace(uint64_t root, uint64_t address, uint64_t size,
                               uint64_t flags);
int lockUserPages(kernel::task::Task& target,uint64_t address,uint64_t size,bool locked,bool allowAbsent=false,bool checkOnly=false);
int lockAllUserPages(kernel::task::Task& target,unsigned flags,const uint64_t* ranges=nullptr,unsigned count=0);
uint64_t getPhysicalAddressInAddressSpace(uint64_t rootPhysicalAddress,
                                          uint64_t virtualAddress,
                                          uint64_t* flags = nullptr);
bool validateUserMemory(uint64_t rootPhysicalAddress, uint64_t address,
                        uint64_t size, bool writable);
UserPageMapResult allocateUserPageInAddressSpace(uint64_t rootPhysicalAddress,
                                                 uint64_t virtualAddress,
                                                 uint64_t flags);
bool copyToAddressSpace(uint64_t rootPhysicalAddress, uint64_t destination,
                        const void* source, uint64_t size);
bool copyFromAddressSpace(void* destination, uint64_t rootPhysicalAddress,
                          uint64_t source, uint64_t size);
uint64_t getKernelAddressSpaceRoot();
// Iterate resident ordinary user pages; caller holds the address-space lock.
bool nextDumpPage(uint64_t root, uint64_t cursor, uint64_t& address,
                  uint64_t& flags);
uint64_t createAddressSpaceRoot();
bool cloneAddressSpaceRoot(uint64_t sourceRootPhysicalAddress,
                           uint64_t destinationRootPhysicalAddress);
void destroyAddressSpaceRoot(uint64_t rootPhysicalAddress);
void switchAddressSpace(uint64_t rootPhysicalAddress);

constexpr uint64_t physicalPageSize   = 4096;
constexpr uint32_t maximumBuddyOrder  = 24;
constexpr uint16_t maximumMemoryNodes = 16;
constexpr uint16_t anyMemoryNode      = UINT16_MAX;
constexpr uint32_t noFrame            = UINT32_MAX;

enum class FrameState : uint8_t { Reserved, Interior, Free, Allocated };
class PageFrame {
    friend class BuddyAlloc;

   public:
    void reset() { *this = PageFrame{}; }

   private:
    uint64_t owner      = 0;
    uint32_t references = 0;
    uint32_t head       = noFrame;
    uint32_t next       = noFrame;
    uint32_t previous   = noFrame;
    uint16_t node       = 0;
    uint8_t order       = 0;
    FrameState state    = FrameState::Reserved;
};
class PhysicalPageInfo {
   public:
    PhysicalPageInfo() = default;
    PhysicalPageInfo(uint64_t owner, uint32_t references, uint16_t node,
                     uint8_t order, FrameState state)
        : owner_(owner),
          references_(references),
          node_(node),
          order_(order),
          state_(state) {}
    uint64_t getOwner() const { return owner_; }
    uint32_t getReferences() const { return references_; }
    uint16_t getNode() const { return node_; }
    uint8_t getOrder() const { return order_; }
    FrameState getState() const { return state_; }

   private:
    uint64_t owner_{};
    uint32_t references_{};
    uint16_t node_{};
    uint8_t order_{};
    FrameState state_{};
};

// The caller supplies metadata; allocation never allocates metadata
// recursively.
class BuddyAlloc {
   public:
    void init(PageFrame* frames, uint32_t count);
    bool addRange(uint32_t first, uint32_t count, uint16_t node);
    uint64_t allocate(uint32_t order, uint64_t owner = 0,
                      uint16_t preferred = anyMemoryNode, bool strict = false,
                      uint64_t limit = UINT64_MAX);
    bool retain(uint64_t address, uint64_t owner = UINT64_MAX);
    bool release(uint64_t address, uint32_t order, uint64_t owner = UINT64_MAX);
    bool assignOwner(uint64_t address, uint64_t owner);
    bool query(uint64_t address, PhysicalPageInfo& info);
    uint64_t pagesOwned(uint64_t owner);
    uint64_t freePages();

   private:
    PageFrame* frames_ = nullptr;
    uint32_t count_    = 0;
    uint32_t heads_[maximumMemoryNodes][maximumBuddyOrder + 1]{};
    uint64_t free_ = 0;
    kernel::task::SpinLock lock_;
    void mark(uint32_t head, uint32_t order, uint16_t node, FrameState state);
    void insert(uint32_t head);
    void remove(uint32_t head);
};

class PhyAlloc {
   public:
    void init(const ::BootInfo* boot);
    uint64_t allocate(uint32_t order, uint64_t owner, uint16_t node,
                      bool strict, uint64_t maximum);
    bool retain(uint64_t address, uint64_t owner) {
        return allocator.retain(address, owner);
    }
    bool release(uint64_t address, uint32_t order, uint64_t owner) {
        return allocator.release(address, order, owner);
    }
    bool assignOwner(uint64_t address, uint64_t owner) {
        return allocator.assignOwner(address, owner);
    }
    bool query(uint64_t address, PhysicalPageInfo& info) {
        return allocator.query(address, info);
    }
    uint64_t ownedBytes(uint64_t owner) {
        return allocator.pagesOwned(owner) * physicalPageSize;
    }
    uint64_t totalBytes() const { return highestAddress; }
    uint64_t freeBytes() { return allocator.freePages() * physicalPageSize; }

   private:
    static constexpr uint64_t limit = 64ULL * 1024 * 1024 * 1024;
    uint8_t available[limit / physicalPageSize / 8]{};
    uint64_t highestAddress = 0;
    BuddyAlloc allocator;
    bool test(uint64_t page) const;
    void set(uint64_t page, bool usable);
    void range(uint64_t first, uint64_t size, bool usable);
};
PhyAlloc& phyAlloc();

uint64_t allocatePhysicalBlock(uint32_t order, uint64_t owner = 0,
                               uint16_t preferred = anyMemoryNode,
                               bool strict        = false,
                               uint64_t limit     = UINT64_MAX);
bool retainPhysicalBlock(uint64_t address, uint64_t owner = UINT64_MAX);
bool releasePhysicalBlock(uint64_t address, uint32_t order,
                          uint64_t owner = UINT64_MAX);
bool assignPhysicalPageOwner(uint64_t address, uint64_t owner);
bool queryPhysicalPage(uint64_t address, PhysicalPageInfo& info);
uint64_t getOwnedMemoryBytes(uint64_t owner);

class PageObject {
   public:
    PageObject(uint32_t order, uint64_t owner, uint16_t node);
    PageObject(const PageObject&)            = delete;
    PageObject& operator=(const PageObject&) = delete;
    bool valid() const { return physical_ != 0; }
    void retain();
    void release();
    uint64_t size() const { return valid() ? 4096ULL << order_ : 0; }
    uint64_t physical() const { return physical_; }
    uint64_t owner() const { return owner_; }
    uint32_t order() const { return order_; }
    uint32_t references() const {
        return __atomic_load_n(&references_, __ATOMIC_ACQUIRE);
    }

   private:
    ~PageObject();
    uint64_t physical_ = 0, owner_ = 0;
    uint32_t references_ = 1, order_ = 0;
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
    kernel::task::SpinLock lock_;
};
class alignas(16) Map {
   public:
    Map() = default;
    Map(kernel::task::Task* targetValue, PageObject* objectValue,
        uint64_t addressValue, uint64_t sizeValue, uint64_t offsetValue,
        uint64_t protectionValue, Map* nextValue)
        : target(targetValue),
          object(objectValue),
          address(addressValue),
          size(sizeValue),
          offset(offsetValue),
          protection(protectionValue),
          next(nextValue) {}
    bool overlaps(uint64_t first, uint64_t length) const {
        return address < first + length && first < address + size;
    }

   private:
    friend class Maps;
    kernel::task::Task* target = nullptr;
    PageObject* object         = nullptr;
    uint64_t address = 0, size = 0, offset = 0, protection = 0;
    Map* next = nullptr;
    bool cow = false, pagerManaged = false;
};
class Maps {
   public:
    bool init();
    bool map(kernel::task::Task& target, PageObject& object, uint64_t address,
             uint64_t size, uint64_t offset, uint64_t protection,
             uint64_t maximumProtection = 3);
    // Caller holds the target address-space lock across this transaction.
    bool prepareUnmap(kernel::task::Task& target, uint64_t address,
                      uint64_t size, void*& reservation);
    void completeUnmap(kernel::task::Task& target, uint64_t address,
                       uint64_t size, void* reservation);
    void releaseTask(kernel::task::Task& target);
    bool cloneTask(kernel::task::Task& source, kernel::task::Task& target);
    bool protectionAllowed(kernel::task::Task& target, uint64_t address,
                           uint64_t size, uint64_t protection);
    bool markCopyOnWrite(kernel::task::Task& target, uint64_t address,
                         uint64_t size);
    bool nextManagedMapping(kernel::task::Task& target, uint64_t cursor,
                            uint64_t& address, uint64_t& size);
    bool dumpAllowed(kernel::task::Task& target, uint64_t address);

   private:
    Map* allocate();
    alignas(16) Map storage[2048]{};
    FixedPool pool;
    kernel::task::SpinLock lock;
    Map* mappings = nullptr;
};
Maps& maps();

uint64_t mapUserPages(kernel::task::Task& target, uint64_t address,
                      uint64_t size, uint64_t protection);
uint64_t unmapUserPages(kernel::task::Task& target, uint64_t address,
                        uint64_t size,bool preserveLocks=false);
uint64_t protectUserPages(kernel::task::Task& target, uint64_t address,
                          uint64_t size, uint64_t protection);

}  // namespace kernel::mm
