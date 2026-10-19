#pragma once
#include "page.h"
#include "os/abi.h"
#include "os/core_service.h"
#include "os/mm.h"

namespace mm {
class KernelMemory {
   public:
    static int bindPager(uint64_t endpoint) {
        os::IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(os::MemoryOperation::BindPager); request.length = 5; request.data[0] = endpoint;
        return os::CoreServiceClient::result(os::CoreServiceKind::Memory, request, reply);
    }
    static bool available(uint64_t task, uint64_t address, uint64_t size) {
        os::IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(os::MemoryOperation::RangeAvailable); request.length = 5;
        request.data[0] = task; request.data[1] = address; request.data[2] = size;
        return os::CoreServiceClient::result(os::CoreServiceKind::Memory, request, reply) == 0 && reply.data[1] == 1;
    }
    static uint64_t pageState(uint64_t task, uint64_t address) {
        os::IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(os::MemoryOperation::PageState); request.length = 5;
        request.data[0] = task; request.data[1] = address;
        return os::CoreServiceClient::result(os::CoreServiceKind::Memory, request, reply) == 0 ? reply.data[1] : 0;
    }
    static int mapShared(uint64_t handle, uint64_t task, uint64_t address, uint64_t size, uint64_t offset, uint64_t protection) {
        os::IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(os::MemoryOperation::MapSharedTarget); request.length = 6;
        request.data[0] = handle; request.data[1] = address; request.data[2] = size; request.data[3] = offset; request.data[4] = protection; request.data[5] = task;
        const int64_t result = os::CoreServiceClient::result(os::CoreServiceKind::Memory, request, reply);
        return result == static_cast<int64_t>(address) ? 0 : result < 0 ? result : -1;
    }
    static uint64_t nativeProtection(uint32_t protection) { return ((protection & 2) ? 1 : 0) | ((protection & 4) ? 2 : 0); }
};
}  // namespace mm

namespace mm {
struct ResidentPage {
    uint64_t task = 0, address = 0, handle = 0, backing = 0, lastUse = 0, fileOffset = 0;
    bool used = false, mapped = false, pinned = false, io = false, cow = false, dirty = false;
};
struct Backing {
    uint64_t id = 0, handle = 0, descriptor = 0, fs = 0;
    BackingKind kind = BackingKind::Anonymous;
    bool used = false, writable = false;
};
class MemoryState {
   public:
    static constexpr size_t pageCapacity = 8192, backingCapacity = 256;
    RegionTable regions;
    ResidentPage pages[pageCapacity]{};
    Backing backings[backingCapacity]{};
    uint64_t clock = 0;
    ResidentPage* page(uint64_t task, uint64_t address);
    ResidentPage* vacantPage();
    Backing* backing(uint64_t id);
    Backing* createBacking(BackingKind kind);
    uint64_t pagesUsed(uint64_t task) const;
    void collectBackings();
    void releaseTask(uint64_t task);
    int synchronize(uint64_t task);
    void sweep();
   private:
    uint64_t nextBacking_ = 1;
    struct Domain { uint64_t task = 0, generation = 0; } domains_[128];
};
}  // namespace mm

namespace mm {
class ReclaimManager {
   public:
    explicit ReclaimManager(MemoryState& state) : state_(state) {}
    uint64_t reclaim(uint64_t bytes);
   private:
    MemoryState& state_;
};
class PageAllocator {
   public:
    PageAllocator(MemoryState& state, ReclaimManager& reclaim) : state_(state), reclaim_(reclaim) {}
    int64_t allocate(uint64_t task);
   private:
    MemoryState& state_;
    ReclaimManager& reclaim_;
};
class SharedMemoryManager {
   public:
    int attach(Backing& backing, Region& region, const os::IpcMessage& request);
};
class Pager {
   public:
    Pager(MemoryState& state, ReclaimManager& reclaim) : state_(state), reclaim_(reclaim), allocator_(state, reclaim) {}
    int fault(uint64_t task, uint64_t address, uint64_t flags);
    int clone(uint64_t parent, uint64_t child);
   private:
    MemoryState& state_;
    ReclaimManager& reclaim_;
    PageAllocator allocator_;
    int loadFile(ResidentPage& page, const Region& region, Backing& backing);
    int copyPrivate(ResidentPage& page, const Region& region);
};
}  // namespace mm
