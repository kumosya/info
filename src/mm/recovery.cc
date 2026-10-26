#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/mm/mm.h"

namespace mm {
uint64_t Recovery::digest(const os::IpcMessage& request) {
    uint64_t value =
        request.type ^ (static_cast<uint64_t>(request.length) << 32);
    for (uint32_t i = 0; i < request.length; ++i)
        value = (value ^ request.data[i]) * 1099511628211ULL;
    return value;
}
void Recovery::collectOutcomes() {
    for (auto& outcome : outcomes_)
        if (outcome.task) {
            os::wire::MemoryUsage usage{};
            bool dead = os::detail::MemoryQuery().usage(usage, outcome.task) ||
                        usage.generation != outcome.generation;
            if (!dead && (outcome.id >> 63)) {
                os::wire::TaskInfo child{};
                dead = os::detail::TaskQuery().query(outcome.id & ~(1ULL << 63),
                                                     child) ||
                       child.state != 0;
            }
            if (dead) {
                if (outcome.handle) os::closeHandle(outcome.handle);
                outcome = {};
            }
        }
}
bool Recovery::lookup(const os::IpcMessage& request, uint64_t id,
                      os::IpcMessage& reply) {
    if (!id) return false;
    os::wire::MemoryUsage usage{};
    if (os::detail::MemoryQuery().usage(usage, request.senderPid)) return false;
    for (const auto& outcome : outcomes_)
        if (outcome.task == request.senderPid &&
            outcome.generation == usage.generation && outcome.id == id) {
            if (outcome.digest != digest(request)) {
                reply.data[0] = static_cast<uint64_t>(-1LL);
                return true;
            }
            os::Runtime::copyBytes(reply.data, outcome.result,
                                   sizeof(outcome.result));
            reply.length = 8;
            if (outcome.handle) {
                const uint32_t rights = outcome.rights ? outcome.rights : 7;
                os::Ipc::Handle handleOwner;
    const int handleStatus = os::Ipc::Handle(outcome.handle, false).duplicate(rights, 0, handleOwner);
    const int64_t handle = handleStatus ? handleStatus : static_cast<int64_t>(handleOwner.release());
                if (handle <= 0) {
                    reply.data[0] = static_cast<uint64_t>(-5LL);
                    return true;
                }
                reply.handleCount      = 1;
                reply.handles[0]       = handle;
                reply.handleRights[0]  = rights;
                reply.handleOptions[0] = os::ipcHandleRestrictRights;
            }
            return true;
        }
    return false;
}
bool Recovery::remember(const os::IpcMessage& request, uint64_t id,
                        const os::IpcMessage& reply) {
    if (!id) return true;
    Outcome* target = nullptr;
    for (auto& outcome : outcomes_) {
        if (outcome.task == request.senderPid && !(outcome.id >> 63) &&
            !(id >> 63)) {
            target = &outcome;
            break;
        }
        if (!outcome.task && !target) target = &outcome;
    }
    if (!target) return false;
    os::wire::MemoryUsage usage{};
    if (os::detail::MemoryQuery().usage(usage, request.senderPid) ||
        usage.generation != state_.generation(request.senderPid))
        return true;
    uint64_t handle = 0;
    if (reply.handleCount) {
        if (!reply.handleRights[0] || (reply.handleRights[0] & ~7U))
            return false;
        os::Ipc::Handle duplicateOwner;
    const int duplicateStatus = os::Ipc::Handle(reply.handles[0], false).duplicate(reply.handleRights[0], 0, duplicateOwner);
    const int64_t duplicate = duplicateStatus ? duplicateStatus : static_cast<int64_t>(duplicateOwner.release());
        if (duplicate <= 0) return false;
        handle = duplicate;
    }
    if (target->handle) os::closeHandle(target->handle);
    *target            = {};
    target->task       = request.senderPid;
    target->generation = usage.generation;
    target->id         = id;
    target->digest     = digest(request);
    target->handle     = handle;
    target->rights     = reply.handleCount ? reply.handleRights[0] : 0;
    os::Runtime::copyBytes(target->result, reply.data, sizeof(target->result));
    return true;
}
bool Recovery::prepare(const os::IpcMessage& request, uint64_t id) {
    collectOutcomes();
    bool capacity = !id;
    for (const auto& outcome : outcomes_)
        if (!outcome.task || (outcome.task == request.senderPid &&
                              !(outcome.id >> 63) && !(id >> 63)))
            capacity = true;
    if (!capacity || checkpoint_.prepare()) return false;
    const auto operation       = static_cast<os::MmOperation>(request.type);
    const bool changesMappings = operation != os::MmOperation::Usage &&
                                 operation != os::MmOperation::Sync &&
                                 operation != os::MmOperation::SharedOpen &&
                                 operation != os::MmOperation::SharedUnlink &&
                                 operation != os::MmOperation::SyncOwned;
    if (!changesMappings) return true;
    os::IpcMessage reply{};
    auto freeze = [&](uint64_t task) {
        if (!task) return true;
        const int result =
            KernelMemory::control(os::MemoryOperation::Freeze, task, 0, reply);
        return !result || result == static_cast<int>(os::ErrorCode::NotFound);
    };
    for (size_t i = 0; i < 128; ++i)
        if (!freeze(state_.domainAt(i))) return false;
    if (!freeze(request.senderPid)) return false;
    return operation != os::MmOperation::Fork || freeze(request.data[0]);
}
bool Recovery::save() {
    capabilities_.begin();
    snapshot_.state    = state_;
    snapshot_.endpoint = capabilities_.encode(endpoint_);
    if (!snapshot_.endpoint) return false;
    for (auto& page : snapshot_.state.pages)
        if (page.used && page.handle) {
            page.handle = capabilities_.encode(page.handle);
            if (!page.handle) return false;
        }
    for (auto& backing : snapshot_.state.backings) {
        if (!backing.used) continue;
        if (!state_.references(backing.id)) {
            backing = {};
            continue;
        }
        if (backing.handle) {
            backing.handle = capabilities_.encode(backing.handle);
            if (!backing.handle) return false;
        }
        backing.fs = 0;
    }
    for (size_t i = 0; i < outcomeCapacity; ++i) {
        snapshot_.outcomes[i] = outcomes_[i];
        if (snapshot_.outcomes[i].handle) {
            snapshot_.outcomes[i].handle =
                capabilities_.encode(snapshot_.outcomes[i].handle);
            if (!snapshot_.outcomes[i].handle) return false;
        }
    }
    if (checkpoint_.commit(&snapshot_) || !capabilities_.collect())
        return false;
    state_.collectBackings(true);
    return true;
}
bool Recovery::validate() const {
    for (size_t i = 0; i < RegionTable::capacity; ++i) {
        const auto& region = state_.regions.at(i);
        if (!region.used) continue;
        if (!RegionTable::validRange(region.address, region.size) ||
            !state_.generation(region.task) || region.protection > 7 ||
            region.maximumProtection > 7 ||
            (region.protection & ~region.maximumProtection) ||
            (region.protection & 6) == 6 || region.kind > BackingKind::Shared)
            return false;
        for (size_t n = 0; n < i; ++n) {
            const auto& other = state_.regions.at(n);
            if (other.used && other.task == region.task &&
                other.address < region.address + region.size &&
                region.address < other.address + other.size)
                return false;
        }
    }
    for (const auto& page : state_.pages)
        if (page.used) {
            const auto* region = state_.regions.find(page.task, page.address);
            if (!region || page.address & 4095 ||
                page.backing != region->backing || page.io)
                return false;
        }
    return true;
}
bool Recovery::restoreFrozen() {
    uint64_t cursor = 1;
    while (true) {
        os::IpcMessage reply{};
        if (KernelMemory::control(os::MemoryOperation::FrozenNext, cursor, 0,
                                  reply))
            return false;
        const uint64_t task = reply.data[1];
        if (!task) return true;
        cursor = task + 1;
        // A frozen task has not observed any uncommitted PTE changes.
        while (true) {
            if (KernelMemory::control(os::MemoryOperation::MappingNext, task, 0,
                                      reply))
                return false;
            if (!reply.data[1]) break;
            if (os::detail::MemoryControl().unmapTarget(reply.data[1],
                                                        reply.data[2], task))
                return false;
        }
        for (auto& page : state_.pages)
            if (page.used && page.task == task && page.mapped) {
                const auto* region  = state_.regions.find(task, page.address);
                const auto* backing = state_.backing(page.backing);
                if (!region || !backing || !region->protection) return false;
                const uint64_t protection =
                    KernelMemory::nativeProtection(region->protection) &
                    (page.cow ? ~1ULL : ~0ULL);
                const int result =
                    backing->kind == BackingKind::Shared
                        ? KernelMemory::mapShared(
                              backing->handle, task, page.address, 4096,
                              region->offset + page.address - region->address,
                              protection)
                    : os::detail::MemoryControl().mapPages(
                          page.handle, page.address, 4096, 0, protection,
                          task) == page.address
                        ? 0
                        : -5;
                if (result ||
                    (page.cow && os::detail::MemoryControl().protectTarget(
                                     page.address, 4096, 4, task)))
                    return false;
            }
    }
}
bool Recovery::activate() {
    uint64_t cursor = 1;
    while (true) {
        os::IpcMessage reply{};
        if (KernelMemory::control(os::MemoryOperation::FrozenNext, cursor, 0,
                                  reply))
            return false;
        const auto task = reply.data[1];
        if (!task) return true;
        cursor = task + 1;
        const int result =
            KernelMemory::control(os::MemoryOperation::Thaw, task, 0, reply);
        if (result && result != static_cast<int>(os::ErrorCode::NotFound))
            return false;
    }
}
bool Recovery::collectFileLeases() {
    const auto fs = os::detail::RegistryClient(os::detail::RegistryClient().endpoint())
                        .lookup(os::fsServiceName, 1000);
    if (!fs) return false;
    bool ok         = true;
    uint64_t cursor = 0;
    while (true) {
        os::IpcMessage request{}, reply{};
        request.type    = static_cast<uint64_t>(os::FsRequest::ListMappings);
        request.length  = 1;
        request.data[0] = cursor;
        if (os::callService(fs, request, reply) ||
            static_cast<int64_t>(reply.data[0])) {
            ok = false;
            break;
        }
        if (!reply.data[1]) break;
        cursor    = reply.data[4];
        bool used = false;
        for (const auto& backing : state_.backings)
            if (backing.used && backing.lease == reply.data[1]) used = true;
        if (!used) {
            request.type = static_cast<uint64_t>(os::FsRequest::ReleaseMapping);
            request.data[0] = reply.data[1];
            if (os::callService(fs, request, reply) ||
                static_cast<int64_t>(reply.data[0])) {
                ok = false;
                break;
            }
        }
    }
    os::closeHandle(fs);
    return ok;
}
bool Recovery::open(uint64_t& endpoint) {
    const int restored =
        checkpoint_.open(&snapshot_, sizeof(snapshot_), 0x4d4d303030303031ULL);
    if (restored < 0) return false;
    capabilities_.begin();
    if (restored) {
        state_ = snapshot_.state;
        state_.pruneRecovered();
        if (!validate()) return false;
        endpoint_ = capabilities_.decode(snapshot_.endpoint);
        for (auto& page : state_.pages)
            if (page.used && page.handle) {
                page.handle = capabilities_.decode(page.handle);
                if (!page.handle) return false;
            }
        for (auto& backing : state_.backings)
            if (backing.used) {
                if (backing.handle) {
                    backing.handle = capabilities_.decode(backing.handle);
                    if (!backing.handle) return false;
                }
                if (backing.lease) {
                    backing.fs =
                        os::detail::RegistryClient(os::detail::RegistryClient().endpoint())
                            .lookup(os::fsServiceName, 1000);
                    if (!backing.fs) return false;
                }
            }
        for (size_t i = 0; i < outcomeCapacity; ++i) {
            const auto& saved = snapshot_.outcomes[i];
            os::wire::MemoryUsage usage{};
            if (!saved.task ||
                os::detail::MemoryQuery().usage(usage, saved.task) ||
                usage.generation != saved.generation)
                continue;
            outcomes_[i] = saved;
            if (saved.handle) {
                outcomes_[i].handle = capabilities_.decode(saved.handle);
                if (!outcomes_[i].handle) return false;
            }
        }
    } else
        endpoint_ = os::detail::EndpointControl().create();
    if (!endpoint_ || !restoreFrozen() || checkpoint_.prepare() || !save() ||
        !capabilities_.discardOrphans() || !collectFileLeases())
        return false;
    endpoint = endpoint_;
    os::Sys().log(restored
                                ? "[MM] VMA and backing checkpoint restored\n"
                                : "[MM] recovery domain initialized\n");
    return true;
}
}  // namespace mm
