#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/mm/mm.h"
#include <os/file.h>
#include <os/ipc.h>
#include <os/service.h>
#include <os/task.h>

namespace mm {
class MemoryManager {
   public:
    int run() {
        const auto registry = os::getBootstrapHandle();
        if (!registry || !recovery_.open(endpoint_) ||
            KernelMemory::bindPager(endpoint_) ||
            !os::detail::RegistryClient(registry).registerName("mm",
                                                               endpoint_) ||
            !recovery_.activate())
            return 1;
        os::Sys().log("[MM] Ring 3 memory policy and pager service ready\n");
        uint64_t nextSweep = 0;
        bool dirty         = false;
        while (true) {
            const uint64_t now = os::Sys().readMonotonicTime();
            if (now >= nextSweep) {
                dirty = state_.sweep() || dirty;
                if (dirty && recovery_.flush()) dirty = false;
                nextSweep = now + 50000000;
            }
            os::IpcMessage request{}, reply{};
            const int received =
                os::receiveMessage(endpoint_, request, nextSweep);
            if (received == static_cast<int>(os::ErrorCode::Timeout)) continue;
            if (received) return 2;
            uint64_t id     = 0;
            bool resumeOnly = false;
            if (request.length >= 3 &&
                request.data[request.length - 3] == recoveryRpcMarker) {
                id         = request.data[request.length - 2];
                resumeOnly = request.data[request.length - 1] != 0;
                request.length -= 3;
            } else if (request.type ==
                           static_cast<uint64_t>(os::MmOperation::Fork) &&
                       request.length == 1)
                id = request.data[0] | (1ULL << 63);
            if (!recovery_.lookup(request, id, reply)) {
                if (resumeOnly)
                    reply.data[0] =
                        static_cast<uint64_t>(os::ErrorCode::PeerDied);
                else if (!recovery_.prepare(request, id)) {
                    os::Sys().log("[MM] checkpoint preparation failed\n");
                    if (!recovery_.activate()) return 2;
                    reply.data[0] =
                        static_cast<uint64_t>(os::ErrorCode::OutOfMemory);
                } else {
                    handle(request, reply);

                    if (!recovery_.remember(request, id, reply) ||
                        !recovery_.save())
                        return 2;

                    if (!recovery_.activate()) return 2;

                    dirty = false;
                }
            }
            reply.length = os::ipcDataWordCount;
            os::replyMessage(endpoint_, request.replyToken, reply);
            for (uint32_t i = 0; i < request.handleCount; ++i)
                os::closeHandle(request.handles[i]);
            for (uint32_t i = 0; i < reply.handleCount; ++i)
                os::closeHandle(reply.handles[i]);
        }
    }

   private:
    MemoryState state_;
    Recovery recovery_{state_};
    ReclaimManager reclaim_{state_};
    Pager pager_{state_, reclaim_};
    SharedMemoryManager shared_;
    uint64_t endpoint_ = 0;

    int unmap(uint64_t task, uint64_t address, uint64_t size) {
        RegionTable staged = state_.regions;
        if (!staged.remove(task, address, size)) return -1;
        bool wasMapped[MemoryState::pageCapacity]{};
        for (size_t i = 0; i < MemoryState::pageCapacity; ++i) {
            auto& page = state_.pages[i];
            if (!page.used || page.task != task || page.address < address ||
                page.address - address >= size || !page.mapped)
                continue;
            wasMapped[i] = true;
            if (os::detail::MemoryControl().unmapTarget(page.address, 4096,
                                                        task)) {
                // Keep backing references until the unmap transaction commits.
                for (size_t n = 0; n < i; ++n)
                    if (wasMapped[n]) {
                        const auto* old =
                            state_.regions.find(task, state_.pages[n].address);
                        if (old)
                            pager_.fault(task, state_.pages[n].address,
                                         (old->protection & 1)   ? 0
                                         : (old->protection & 4) ? 16
                                                                 : 2);
                    }
                return -11;
            }
            page.mapped = false;
        }
        state_.regions = staged;
        for (auto& page : state_.pages)
            if (page.used && page.task == task && page.address >= address &&
                page.address - address < size) {
                if (page.handle) os::closeHandle(page.handle);
                page = {};
            }
        state_.collectBackings();
        return 0;
    }
    int protect(uint64_t task, uint64_t address, uint64_t size,
                uint32_t protection) {
        RegionTable staged = state_.regions;
        if (!staged.protect(task, address, size, protection)) return -3;
        bool changed[MemoryState::pageCapacity]{};
        for (size_t index = 0; index < MemoryState::pageCapacity; ++index) {
            auto& page = state_.pages[index];
            if (!page.used || page.task != task || page.address < address ||
                page.address - address >= size || !page.mapped)
                continue;
            page.dirty =
                page.dirty ||
                (KernelMemory::pageState(task, page.address) & (1ULL << 6));
            const int result =
                protection ? os::detail::MemoryControl().protectTarget(
                                 page.address, 4096,
                                 KernelMemory::nativeProtection(protection) &
                                     (page.cow ? ~1ULL : ~0ULL),
                                 task)
                           : os::detail::MemoryControl().unmapTarget(
                                 page.address, 4096, task);
            if (result) {
                // Restore PTE permissions from the uncommitted region table.
                for (size_t n = 0; n < index; ++n)
                    if (changed[n]) {
                        auto& previous = state_.pages[n];
                        const auto* old =
                            state_.regions.find(task, previous.address);
                        if (!old) continue;
                        if (!protection)
                            pager_.fault(task, previous.address,
                                         (old->protection & 1)   ? 0
                                         : (old->protection & 4) ? 16
                                                                 : 2);
                        else
                            os::detail::MemoryControl().protectTarget(
                                previous.address, 4096,
                                KernelMemory::nativeProtection(
                                    old->protection) &
                                    (previous.cow ? ~1ULL : ~0ULL),
                                task);
                    }
                return result;
            }
            changed[index] = true;
            if (!protection) page.mapped = false;
        }
        state_.regions = staged;
        return 0;
    }
    int64_t reserve(const os::IpcMessage& request, BackingKind kind) {
        const auto* args      = request.data;
        const uint64_t size   = args[0];
        const auto protection = static_cast<uint32_t>(args[1]);
        const uint64_t hint =
            kind == BackingKind::Anonymous ? args[3] : args[4];
        if (!size || (size & 4095) || size > 32ULL * 1024 * 1024 ||
            args[1] > 7 || (args[1] & 6) == 6 || (args[2] & ~7ULL))
            return -1;

        Backing* backing = state_.createBacking(kind);
        if (!backing)
            return -5;
        Region region{};
        region.task              = request.senderPid;
        region.size              = size;
        region.backing           = backing->id;
        region.protection        = protection;
        region.maximumProtection = 7;
        region.kind              = kind;
        region.lazy              = !(args[2] & os::mmFlagEager);
        region.shared            = args[2] & os::mmFlagShared;
        if (kind == BackingKind::Shared) {
            const int result = shared_.attach(*backing, region, request);
            if (result) {
                state_.collectBackings();
                return result;
            }
        }
        if (kind == BackingKind::File) {
            if ((args[2] & os::mmFlagShared) || (args[3] & 4095) ||
                args[3] > UINT64_MAX - size || request.length < 6) {
                state_.collectBackings();
                return -1;
            }
            // Only the protected FS service can validate and redeem a
            // delegation.
            const uint64_t fs = os::detail::RegistryClient(
                                    os::detail::RegistryClient().endpoint())
                                    .lookup(os::fsServiceName, 1);
            os::IpcMessage claim{}, reply{};
            claim.type    = static_cast<uint64_t>(os::FsRequest::ClaimMapping);
            claim.length  = 2;
            claim.data[0] = args[5];
            claim.data[1] = request.senderPid;
            const int result = fs ? os::callService(fs, claim, reply) : -4;
            if (result || static_cast<int64_t>(reply.data[0])) {
                if (fs) os::closeHandle(fs);
                state_.collectBackings();
                return result ? result : static_cast<int64_t>(reply.data[0]);
            }
            backing->fs         = fs;
            backing->descriptor = reply.data[1];
            backing->lease      = reply.length >= 4 ? reply.data[3] : 0;
            region.offset       = args[3];
        }
        uint64_t candidate = hint ? hint : 0x50000000;
        for (unsigned attempt = 0; attempt < 16; ++attempt) {
            region.address =
                state_.regions.findGap(region.task, size, candidate);
            if (!region.address) break;
            int availabilityError = 0;
            if (KernelMemory::available(region.task, region.address, size,
                                        &availabilityError))
                break;
            if (availabilityError) {
                state_.collectBackings();
                return availabilityError;
            }
            candidate      = region.address + size;
            region.address = 0;
        }
        if (!region.address || !state_.regions.insert(region)) {
            os::Sys().log("[MM] virtual address reservation failed\n");
            state_.collectBackings();
            return -5;
        }
        if (!region.lazy && protection)
            for (uint64_t offset = 0; offset < size; offset += 4096) {
                const int result =
                    pager_.fault(region.task, region.address + offset,
                                 (protection & 1)   ? 0
                                 : (protection & 4) ? 16
                                                    : 2);
                if (result) {
                    unmap(region.task, region.address, region.size);
                    return result;
                }
            }
        return region.address;
    }
    void handle(const os::IpcMessage& request, os::IpcMessage& reply) {
        reply.data[0] = static_cast<int64_t>(-1);
        if (!request.senderPid) return;
        const int synchronized = state_.synchronize(request.senderPid);
        if (synchronized < 0) {
            reply.data[0] = synchronized;
            return;
        }
        const auto operation = static_cast<os::MmOperation>(request.type);
        if (synchronized && operation != os::MmOperation::MapAnonymous &&
            operation != os::MmOperation::MapShared &&
            operation != os::MmOperation::MapFile) {
            reply.data[0] = static_cast<int64_t>(os::ErrorCode::PeerDied);
            return;
        }
        switch (operation) {
            case os::MmOperation::MapAnonymous:
                if (request.length >= 4)
                    reply.data[0] = reserve(request, BackingKind::Anonymous);
                if (static_cast<int64_t>(reply.data[0]) > 0) {
                    const auto* region =
                        state_.regions.find(request.senderPid, reply.data[0]);
                    if (region) reply.data[1] = region->backing;
                }
                break;
            case os::MmOperation::MapShared:
                if (request.length >= 5)
                    reply.data[0] = reserve(request, BackingKind::Shared);
                if (static_cast<int64_t>(reply.data[0]) > 0) {
                    const auto* region =
                        state_.regions.find(request.senderPid, reply.data[0]);
                    if (region) reply.data[1] = region->backing;
                }
                break;
            case os::MmOperation::MapFile:
                if (request.length >= 6)
                    reply.data[0] = reserve(request, BackingKind::File);
                if (static_cast<int64_t>(reply.data[0]) > 0) {
                    const auto* region =
                        state_.regions.find(request.senderPid, reply.data[0]);
                    if (region) reply.data[1] = region->backing;
                }
                break;
            case os::MmOperation::Fault:
                if (request.length >= 2)
                    reply.data[0] = pager_.fault(
                        request.senderPid, request.data[0], request.data[1]);
                break;
            case os::MmOperation::Fork:
                if (request.length == 1)
                    reply.data[0] =
                        pager_.clone(request.senderPid, request.data[0]);
                break;
            case os::MmOperation::Unmap:
                if (request.length >= 2)
                    reply.data[0] = unmap(request.senderPid, request.data[0],
                                          request.data[1]);
                break;
            case os::MmOperation::Protect:
                if (request.length >= 3 && request.data[2] <= 7)
                    reply.data[0] = protect(request.senderPid, request.data[0],
                                            request.data[1], request.data[2]);
                break;
            case os::MmOperation::UnmapOwned:
            case os::MmOperation::ProtectOwned:
            case os::MmOperation::SyncOwned: {
                if (request.length != 5 ||
                    !RegionTable::validRange(request.data[0],
                                             request.data[1]) ||
                    !request.data[2])
                    break;
                if (operation == os::MmOperation::ProtectOwned &&
                    request.data[3] > 7)
                    break;
                const uint64_t address  = request.data[0],
                               size     = request.data[1],
                               identity = request.data[2];
                if (operation == os::MmOperation::UnmapOwned) {
                    reply.data[0] = 0;
                    for (size_t index = 0; index < RegionTable::capacity;
                         ++index) {
                        const auto region = state_.regions.at(index);
                        if (!region.used || region.task != request.senderPid ||
                            region.backing != identity ||
                            region.address >= address + size ||
                            address >= region.address + region.size)
                            continue;
                        const uint64_t first =
                            region.address > address ? region.address : address;
                        const uint64_t end =
                            region.address + region.size < address + size
                                ? region.address + region.size
                                : address + size;
                        const int status =
                            unmap(request.senderPid, first, end - first);
                        if (status) {
                            reply.data[0] = status;
                            break;
                        }
                        index = static_cast<size_t>(-1);
                    }
                } else {
                    uint64_t cursor = address;
                    while (cursor < address + size) {
                        const auto* region =
                            state_.regions.find(request.senderPid, cursor);
                        if (!region || region->backing != identity) break;
                        cursor = region->address + region->size;
                    }
                    if (cursor < address + size) {
                        reply.data[0] =
                            static_cast<int64_t>(os::ErrorCode::NotFound);
                        break;
                    }
                    reply.data[0] = operation == os::MmOperation::ProtectOwned
                                        ? protect(request.senderPid, address,
                                                  size, request.data[3])
                                        : 0;
                }
                break;
            }
            case os::MmOperation::Usage: {
                os::wire::MemoryUsage usage{};
                const int result =
                    os::detail::MemoryQuery().usage(usage, request.senderPid);
                reply.data[0] = result;
                if (!result)
                    os::Runtime::copyBytes(&reply.data[1], &usage,
                                           sizeof(usage));
                break;
            }
            case os::MmOperation::Reclaim:
                reply.data[0] = reclaim_.reclaim(4096);
                break;
            case os::MmOperation::Sync:
                if (request.length == 2)
                    reply.data[0] =
                        state_.regions.covered(request.senderPid,
                                               request.data[0], request.data[1])
                            ? 0
                            : -4;
                break;
            case os::MmOperation::SharedOpen:
            case os::MmOperation::SharedUnlink: {
                if (request.length != 9) break;
                const char* name =
                    reinterpret_cast<const char*>(&request.data[1]);
                size_t length = 0;
                while (length < 64 && name[length]) ++length;
                if (length < 2 || length >= 64 || name[0] != '/') break;
                if (operation == os::MmOperation::SharedUnlink)
                    reply.data[0] =
                        os::detail::MemoryControl().unlinkSharedMemory(name);
                else {
                    const int64_t handle =
                        os::detail::MemoryControl().openSharedMemory(
                            name, request.data[0]);
                    reply.data[0] = handle > 0 ? 0 : handle ? handle : -4;
                    if (handle > 0) {
                        reply.handleCount = 1;
                        reply.handles[0]  = handle;
                        reply.handleRights[0] =
                            (request.data[0] & 3) == 0 ? 1 : 7;
                        reply.handleOptions[0] = os::ipcHandleRestrictRights;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
};
}  // namespace mm
int main(int, char**) {
    static mm::MemoryManager manager;
    return manager.run();
}
