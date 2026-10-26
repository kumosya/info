#include "service/common/channel.h"
#include "service/common/queries.h"
#pragma once
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>
#include "service/common/kernel-requests.h"
#include "service/common/recovery.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/mm/page.h"

namespace mm {
class KernelMemory {
   public:
    static int control(os::MemoryOperation operation, uint64_t task,
                       uint64_t cursor, os::IpcMessage& reply) {
        os::IpcMessage request{};
        request.type    = static_cast<uint64_t>(operation);
        request.length  = 5;
        request.data[0] = task;
        request.data[1] = cursor;
        return services::RequestChannel(os::CoreServiceKind::Memory)
            .result(request, reply);
    }
    static int bindPager(uint64_t endpoint) {
        os::IpcMessage request{}, reply{};
        request.type    = static_cast<uint64_t>(os::MemoryOperation::BindPager);
        request.length  = 5;
        request.data[0] = endpoint;
        return services::RequestChannel(os::CoreServiceKind::Memory)
            .result(request, reply);
    }
    static bool available(uint64_t task, uint64_t address, uint64_t size,
                          int* error = nullptr) {
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(os::MemoryOperation::RangeAvailable);
        request.length   = 5;
        request.data[0]  = task;
        request.data[1]  = address;
        request.data[2]  = size;
        const int status = services::RequestChannel(os::CoreServiceKind::Memory)
                               .result(request, reply);
        if (error) *error = status;
        return status == 0 && reply.data[1] == 1;
    }
    static uint64_t pageState(uint64_t task, uint64_t address) {
        os::IpcMessage request{}, reply{};
        request.type    = static_cast<uint64_t>(os::MemoryOperation::PageState);
        request.length  = 5;
        request.data[0] = task;
        request.data[1] = address;
        return services::RequestChannel(os::CoreServiceKind::Memory)
                           .result(request, reply) == 0
                   ? reply.data[1]
                   : 0;
    }
    static int mapShared(uint64_t handle, uint64_t task, uint64_t address,
                         uint64_t size, uint64_t offset, uint64_t protection) {
        const int64_t result = os::detail::MemoryControl().mapSharedMemory(
            handle, address, size, offset, protection, task);
        return result == static_cast<int64_t>(address) ? 0
               : result < 0                            ? result
                                                       : -1;
    }
    static uint64_t nativeProtection(uint32_t protection) {
        return ((protection & 2) ? 1 : 0) | ((protection & 4) ? 2 : 0);
    }
};
}  // namespace mm

namespace mm {
struct ResidentPage {
    uint64_t task = 0, address = 0, handle = 0, backing = 0, lastUse = 0,
             fileOffset = 0;
    bool used = false, mapped = false, pinned = false, io = false, cow = false,
         dirty = false;
};
struct Backing {
    uint64_t id = 0, handle = 0, descriptor = 0, fs = 0, lease = 0;
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
    bool references(uint64_t backing) const;
    void collectBackings(bool releaseFiles = false);
    void releaseTask(uint64_t task);
    int synchronize(uint64_t task);
    bool sweep();
    void pruneRecovered();
    uint64_t domainAt(size_t index) const {
        return index < 128 ? domains_[index].task : 0;
    }
    uint64_t generation(uint64_t task) const {
        for (const auto& domain : domains_)
            if (domain.task == task) return domain.generation;
        return 0;
    }

   private:
    uint64_t nextBacking_ = 1;
    struct Domain {
        uint64_t task = 0, generation = 0;
    } domains_[128];
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
    PageAllocator(MemoryState& state, ReclaimManager& reclaim)
        : state_(state), reclaim_(reclaim) {}
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
    Pager(MemoryState& state, ReclaimManager& reclaim)
        : state_(state), reclaim_(reclaim), allocator_(state, reclaim) {}
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

namespace mm {
constexpr uint64_t recoveryRpcMarker = 0x4d4d525043303031ULL;
class Recovery {
   public:
    explicit Recovery(MemoryState& state)
        : state_(state), capabilities_(checkpoint_) {}
    bool open(uint64_t& endpoint);
    bool prepare(const os::IpcMessage& request, uint64_t id);
    bool flush() { return !checkpoint_.prepare() && save(); }
    bool save();
    bool activate();
    bool lookup(const os::IpcMessage& request, uint64_t id,
                os::IpcMessage& reply);
    bool remember(const os::IpcMessage& request, uint64_t id,
                  const os::IpcMessage& reply);

   private:
    static constexpr size_t outcomeCapacity = 256;
    struct Outcome {
        uint64_t task = 0, generation = 0, id = 0, digest = 0, handle = 0;
        uint64_t result[7]{};
        uint32_t rights = 0, reserved = 0;
    } outcomes_[outcomeCapacity];
    static_assert(sizeof(Outcome) == 104);
    struct Snapshot {
        uint64_t endpoint = 0;
        MemoryState state;
        Outcome outcomes[outcomeCapacity];
    } snapshot_;
    MemoryState& state_;
    os::Sys::Checkpoint checkpoint_;
    services::RecoveryCapabilities capabilities_;
    uint64_t endpoint_ = 0;
    bool validate() const;
    bool restoreFrozen();
    bool collectFileLeases();
    void collectOutcomes();
    static uint64_t digest(const os::IpcMessage& request);
};
}  // namespace mm
