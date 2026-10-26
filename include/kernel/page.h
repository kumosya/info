#pragma once

#include <cstdint>
#include "kernel/spinlock.h"

namespace kernel {

struct BootInfo;

constexpr uint64_t pagePresent      = 1ULL << 0;
constexpr uint64_t pageWritable     = 1ULL << 1;
constexpr uint64_t pageUser         = 1ULL << 2;
constexpr uint64_t pageWriteThrough = 1ULL << 3;
constexpr uint64_t pageCacheDisable = 1ULL << 4;
constexpr uint64_t pageGlobal       = 1ULL << 8;
constexpr uint64_t pageExternal     = 1ULL << 9;
constexpr uint64_t pageNoExecute    = 1ULL << 63;

enum class UserPageMapResult : uint8_t {
    Success,
    InvalidArgument,
    OutOfMemory,
    MappingFailed
};

namespace memory {
    void initVir(const BootInfo* bootInfo);
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
    bool nextDumpPage(uint64_t root, uint64_t cursor, uint64_t& address, uint64_t& flags);
    uint64_t createAddressSpaceRoot();
    bool cloneAddressSpaceRoot(uint64_t sourceRootPhysicalAddress,
                            uint64_t destinationRootPhysicalAddress);
    void destroyAddressSpaceRoot(uint64_t rootPhysicalAddress);
    void switchAddressSpace(uint64_t rootPhysicalAddress);

}  // namespace memory
}  // namespace kernel

namespace kernel::memory {
constexpr uint64_t physicalPageSize = 4096;
constexpr uint32_t maximumBuddyOrder = 24;
constexpr uint16_t maximumMemoryNodes = 16;
constexpr uint16_t anyMemoryNode = UINT16_MAX;
constexpr uint32_t noFrame = UINT32_MAX;

enum class FrameState : uint8_t { Reserved, Interior, Free, Allocated };
struct PageFrame {
    uint64_t owner = 0;
    uint32_t references = 0;
    uint32_t head = noFrame;
    uint32_t next = noFrame;
    uint32_t previous = noFrame;
    uint16_t node = 0;
    uint8_t order = 0;
    FrameState state = FrameState::Reserved;
};
struct PhysicalPageInfo {
    uint64_t owner;
    uint32_t references;
    uint16_t node;
    uint8_t order;
    FrameState state;
};

// The caller supplies metadata; allocation never allocates metadata recursively.
class BuddyAllocator {
   public:
    void init(PageFrame* frames, uint32_t count);
    bool addRange(uint32_t first, uint32_t count, uint16_t node);
    uint64_t allocate(uint32_t order, uint64_t owner = 0, uint16_t preferred = anyMemoryNode,
                      bool strict = false, uint64_t limit = UINT64_MAX);
    bool retain(uint64_t address, uint64_t owner = UINT64_MAX);
    bool release(uint64_t address, uint32_t order, uint64_t owner = UINT64_MAX);
    bool assignOwner(uint64_t address, uint64_t owner);
    bool query(uint64_t address, PhysicalPageInfo& info);
    uint64_t pagesOwned(uint64_t owner);
    uint64_t freePages();
   private:
    PageFrame* frames_ = nullptr;
    uint32_t count_ = 0;
    uint32_t heads_[maximumMemoryNodes][maximumBuddyOrder + 1]{};
    uint64_t free_ = 0;
    SpinLock lock_;
    void mark(uint32_t head, uint32_t order, uint16_t node, FrameState state);
    void insert(uint32_t head);
    void remove(uint32_t head);
};

uint64_t allocatePhysicalBlock(uint32_t order, uint64_t owner = 0,
    uint16_t preferred = anyMemoryNode, bool strict = false, uint64_t limit = UINT64_MAX);
bool retainPhysicalBlock(uint64_t address, uint64_t owner = UINT64_MAX);
bool releasePhysicalBlock(uint64_t address, uint32_t order, uint64_t owner = UINT64_MAX);
bool assignPhysicalPageOwner(uint64_t address, uint64_t owner);
bool queryPhysicalPage(uint64_t address, PhysicalPageInfo& info);
uint64_t getOwnedMemoryBytes(uint64_t owner);
}  // namespace kernel::memory

namespace kernel { class Task; }
namespace kernel::memory {
class PageObject {
   public:
    PageObject(uint32_t order, uint64_t owner, uint16_t node);
    PageObject(const PageObject&) = delete;
    PageObject& operator=(const PageObject&) = delete;
    bool valid() const { return physical_ != 0; }
    void retain();
    void release();
    uint64_t size() const { return valid() ? 4096ULL << order_ : 0; }
    uint64_t physical() const { return physical_; }
    uint64_t owner() const { return owner_; }
    uint32_t order() const { return order_; }
    uint32_t references() const { return __atomic_load_n(&references_, __ATOMIC_ACQUIRE); }
   private:
    ~PageObject();
    uint64_t physical_ = 0, owner_ = 0;
    uint32_t references_ = 1, order_ = 0;
};
class PageMappings {
   public:
    static bool init();
    static bool map(Task& target, PageObject& object, uint64_t address, uint64_t size,
                    uint64_t offset, uint64_t protection, uint64_t maximumProtection = 3);
    // Caller holds the target address-space lock across this transaction.
    static bool prepareUnmap(Task& target, uint64_t address, uint64_t size, void*& reservation);
    static void completeUnmap(Task& target, uint64_t address, uint64_t size, void* reservation);
    static void releaseTask(Task& target);
    static bool cloneTask(Task& source, Task& target);
    static bool protectionAllowed(Task& target, uint64_t address, uint64_t size, uint64_t protection);
    static bool markCopyOnWrite(Task& target, uint64_t address, uint64_t size);
    static bool nextManagedMapping(Task& target, uint64_t cursor, uint64_t& address, uint64_t& size);
    static bool dumpAllowed(Task& target, uint64_t address);
};
}  // namespace kernel::memory

namespace kernel { class Task; }
namespace kernel::memory {
class MappingOperations {
   public:
    static uint64_t map(Task& target, uint64_t address, uint64_t size, uint64_t protection);
    static uint64_t unmap(Task& target, uint64_t address, uint64_t size);
    static uint64_t protect(Task& target, uint64_t address, uint64_t size, uint64_t protection);
};
}  // namespace kernel::memory
