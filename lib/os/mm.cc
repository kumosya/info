#include <fcntl.h>

#include "service/common/channel.h"
#include "service/common/entry.h"
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/console/client.h"
#include <os/console.h>
#include <os/device.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>
#include <sys/mman.h>

namespace os {

detail::MemoryQuery::MemoryQuery() : Sys::Transport(CoreServiceKind::Memory) {}

namespace {

class RuntimeHeap {
    class Allocation {
       public:
        uint64_t getAddress() const { return address_; }
        uint64_t getSize() const { return size_; }
        void assign(uint64_t address, uint64_t size) {
            address_ = address;
            size_    = size;
        }
        void clear() { address_ = size_ = 0; }

       private:
        uint64_t address_{}, size_{};
    } allocations_[4096];
    uint64_t nextAddress_ = 0x10000000;
    uint8_t lock_{};
    void lock() {
        while (__atomic_test_and_set(&lock_, __ATOMIC_ACQUIRE))
            asm volatile("pause");
    }
    void unlock() { __atomic_clear(&lock_, __ATOMIC_RELEASE); }

   public:
    void initialize() {
        lock_        = 0;
        nextAddress_ = 0x10000000;
        for (auto& allocation : allocations_) allocation.clear();
    }
    void* allocate(size_t size) {
        if (!size || size > UINT64_MAX - 4095) return nullptr;
        const uint64_t aligned = (size + 4095) & ~4095ULL;
        lock();
        for (auto& allocation : allocations_)
            if (!allocation.getAddress()) {
                const uint64_t address = nextAddress_;
                if (aligned > UINT64_MAX - 4096 - address ||
                    mapMemory(address, aligned, 1) != address) {
                    unlock();
                    return nullptr;
                }
                allocation.assign(address, aligned);
                nextAddress_ += aligned + 4096;
                unlock();
                return reinterpret_cast<void*>(address);
            }
        unlock();
        return nullptr;
    }
    bool free(void* pointer) {
        if (!pointer) return true;
        const auto address = reinterpret_cast<uint64_t>(pointer);
        lock();
        for (auto& allocation : allocations_)
            if (allocation.getAddress() == address) {
                const bool success = unmapMemory(address, allocation.getSize());
                if (success) allocation.clear();
                unlock();
                return success;
            }
        unlock();
        return false;
    }
} runtimeHeap;

}  // namespace
namespace {
class VirtualMemoryClient {
    uint64_t endpoint_ = 0;
    uint64_t sequence_ = 0;
    uint8_t lock_      = 0;

   public:
    int64_t call(IpcMessage& request, uint64_t* identity = nullptr) {
        while (__atomic_test_and_set(&lock_, __ATOMIC_ACQUIRE)) yield();
        if (request.length > ipcDataWordCount - 3) {
            __atomic_clear(&lock_, __ATOMIC_RELEASE);
            return static_cast<int>(ErrorCode::InvalidArgument);
        }
        const uint32_t original    = request.length;
        request.data[original]     = 0x4d4d525043303031ULL;
        request.data[original + 1] = ++sequence_;
        request.data[original + 2] = 0;
        request.length += 3;
        const uint64_t deadline = Sys().readMonotonicTime() + 20000000000ULL;
        IpcMessage reply{};
        int status = 0;
        while (true) {
            if (!endpoint_) {
                endpoint_ = os::detail::RegistryClient(
                                os::detail::RegistryClient().endpoint())
                                .lookup("mm", 1);
                if (endpoint_)
                    Ipc::Handle(endpoint_, false)
                        .setFlags(handleFlagCloseOnExec);
            }
            status = endpoint_
                         ? callService(endpoint_, request, reply, deadline)
                         : static_cast<int>(ErrorCode::NotFound);
            const bool recovering =
                status == static_cast<int>(ErrorCode::PeerDied) ||
                status == static_cast<int>(ErrorCode::EndpointClosed) ||
                status == static_cast<int>(ErrorCode::InvalidHandle) ||
                status == static_cast<int>(ErrorCode::NotFound);
            if (!recovering) break;
            if (endpoint_) closeHandle(endpoint_);
            endpoint_ = 0;
            // One-shot FS delegations and namespace changes may only resume a
            // committed result.
            if (request.type == static_cast<uint64_t>(MmOperation::MapFile) ||
                request.type ==
                    static_cast<uint64_t>(MmOperation::SharedOpen) ||
                request.type ==
                    static_cast<uint64_t>(MmOperation::SharedUnlink))
                request.data[original + 2] = 1;
            if (Sys().readMonotonicTime() >= deadline) {
                status = static_cast<int>(ErrorCode::Timeout);
                break;
            }
            sleep(10);
        }
        if (identity)
            *identity = (!status && reply.length >= 2) ? reply.data[1] : 0;
        request.length = original;
        __atomic_clear(&lock_, __ATOMIC_RELEASE);
        return status              ? status
               : reply.handleCount ? static_cast<int64_t>(reply.handles[0])
                                   : static_cast<int64_t>(reply.data[0]);
    }
} virtualMemoryClient;
}  // namespace
uint64_t detail::MemoryControl::allocatePages(uint32_t order, uint16_t node,
                                              uint64_t owner) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::AllocatePages);
    request.length  = 5;
    request.data[0] = order;
    request.data[1] = node;
    request.data[2] = owner;
    return this->result(request, reply);
}

uint64_t detail::MemoryControl::mapPages(uint64_t handle, uint64_t address,
                                         uint64_t size, uint64_t offset,
                                         uint64_t protection, uint64_t target) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Map);
    request.length  = 6;
    request.data[0] = address;
    request.data[1] = size;
    request.data[2] = protection | OS_MAP_PAGE_OBJECT;
    request.data[3] = handle;
    request.data[4] = offset;
    request.data[5] = target;
    return this->result(request, reply);
}

int detail::MemoryQuery::pageInfo(uint64_t handle,
                                  os::wire::PageObjectInfo& info) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(MemoryOperation::PageInfo);
    request.length       = 5;
    request.data[0]      = handle;
    const int64_t result = this->result(request, reply);
    if (!result && reply.length < 1 + (sizeof(info) + 7) / 8)
        return static_cast<int>(ErrorCode::IoError);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}

int detail::MemoryQuery::usage(os::wire::MemoryUsage& info, uint64_t target) {
    IpcMessage request{}, reply{};
    request.type         = static_cast<uint64_t>(MemoryOperation::Usage);
    request.length       = 5;
    request.data[0]      = target;
    const int64_t result = this->result(request, reply);
    if (!result && reply.length < 1 + (sizeof(info) + 7) / 8)
        return static_cast<int>(ErrorCode::IoError);
    if (!result) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return result;
}

int64_t detail::MemoryControl::reclaimCaches() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(MemoryOperation::ReclaimCaches);
    request.length = 5;
    return this->result(request, reply);
}

int detail::MemoryControl::unmapTarget(uint64_t address, uint64_t size,
                                       uint64_t target) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Unmap);
    request.length  = 5;
    request.data[0] = address;
    request.data[1] = size;
    request.data[2] = target;
    return this->result(request, reply);
}

int detail::MemoryControl::protectTarget(uint64_t address, uint64_t size,
                                         uint64_t protection, uint64_t target) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Protect);
    request.length  = 5;
    request.data[0] = address;
    request.data[1] = size;
    request.data[2] = protection;
    request.data[3] = target;
    return this->result(request, reply);
}

uint64_t detail::MemoryControl::openSharedMemory(const char* name, int flags) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Open);
    request.length  = 5;
    request.data[0] = static_cast<uint32_t>(flags);
    if (!name) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    Runtime::copyString(reinterpret_cast<char*>(&request.data[5]), name, 64);
    request.length = 5 + 8;
    return this->result(request, reply);
}

int detail::MemoryControl::unlinkSharedMemory(const char* name) {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(MemoryOperation::Unlink);
    request.length = 5;
    if (!name) return static_cast<int64_t>(ErrorCode::InvalidArgument);
    Runtime::copyString(reinterpret_cast<char*>(&request.data[5]), name, 64);
    request.length = 5 + 8;
    return this->result(request, reply);
}

int detail::MemoryControl::resizeSharedMemory(uint64_t handle, uint64_t size) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Resize);
    request.length  = 5;
    request.data[0] = handle;
    request.data[1] = size;
    return this->result(request, reply);
}

uint64_t detail::MemoryControl::mapSharedMemory(uint64_t handle,
                                                uint64_t address, uint64_t size,
                                                uint64_t offset,
                                                uint64_t protection,
                                                uint64_t target) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Map);
    request.length  = 6;
    request.data[0] = address;
    request.data[1] = size;
    request.data[2] = protection | OS_MAP_SHARED_OBJECT;
    request.data[3] = handle;
    request.data[4] = offset;
    request.data[5] = target;
    return this->result(request, reply);
}

void* Runtime::allocateMemory(size_t size) {
    return runtimeHeap.allocate(size);
}
bool Runtime::freeMemory(void* pointer) { return runtimeHeap.free(pointer); }

uint64_t detail::MappingClient::anonymous(uint64_t size, uint32_t protection,
                                          bool lazy, uint64_t hint,
                                          uint64_t* identity) {
    if (!size || size > UINT64_MAX - 4095)
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(MmOperation::MapAnonymous);
    request.length  = 4;
    request.data[0] = (size + 4095) & ~4095ULL;
    request.data[1] = protection;
    request.data[2] = (lazy ? 0 : mmFlagEager) | (identity ? 4 : 0);
    request.data[3] = hint;
    return virtualMemoryClient.call(request, identity);
}

uint64_t detail::MappingClient::shared(uint64_t handle, uint64_t size,
                                       uint32_t protection, uint64_t offset,
                                       uint64_t hint, uint64_t* identity) {
    if (!handle || !size || size > UINT64_MAX - 4095)
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type        = static_cast<uint64_t>(MmOperation::MapShared);
    request.length      = 5;
    request.handleCount = 1;
    request.handles[0]  = handle;
    request.data[0]     = (size + 4095) & ~4095ULL;
    request.data[1]     = protection;
    request.data[2]     = mmFlagShared | (identity ? 4 : 0);
    request.data[3]     = offset;
    request.data[4]     = hint;
    return virtualMemoryClient.call(request, identity);
}

uint64_t detail::MappingClient::file(uint64_t descriptor, uint64_t size,
                                     uint32_t protection, uint64_t offset,
                                     uint64_t hint, uint64_t* identity) {
    if (!descriptor || !size || size > UINT64_MAX - 4095)
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    const uint64_t fs =
        os::detail::RegistryClient(os::detail::RegistryClient().endpoint())
            .lookup(fsServiceName, 1);
    if (!fs) return static_cast<int64_t>(ErrorCode::NotFound);
    IpcMessage delegate{}, reply{};
    delegate.type    = static_cast<uint64_t>(FsRequest::DelegateMapping);
    delegate.length  = 1;
    delegate.data[0] = descriptor;
    const int status = callService(fs, delegate, reply);
    if (status || static_cast<int64_t>(reply.data[0])) {
        closeHandle(fs);
        return status ? status : reply.data[0];
    }
    IpcMessage request{};
    request.type         = static_cast<uint64_t>(MmOperation::MapFile);
    request.length       = 6;
    request.data[0]      = (size + 4095) & ~4095ULL;
    request.data[1]      = protection;
    request.data[2]      = identity ? 4 : 0;
    request.data[3]      = offset;
    request.data[4]      = hint;
    request.data[5]      = reply.data[1];
    const int64_t result = virtualMemoryClient.call(request, identity);
    // An unclaimed delegation belongs to the issuer and must not leak on
    // failure.
    if (result < 0) {
        IpcMessage close{}, ignored{};
        close.type    = static_cast<uint64_t>(FsRequest::CancelMapping);
        close.length  = 1;
        close.data[0] = reply.data[1];
        callService(fs, close, ignored);
    }
    closeHandle(fs);
    return result;
}

int detail::MappingClient::unmap(uint64_t address, uint64_t size) {
    if (!size || size > UINT64_MAX - 4095)
        return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(MmOperation::Unmap);
    request.length  = 2;
    request.data[0] = address;
    request.data[1] = (size + 4095) & ~4095ULL;
    return virtualMemoryClient.call(request);
}

int detail::MappingClient::protect(uint64_t address, uint64_t size,
                                   uint32_t protection) {
    if (!size || size > UINT64_MAX - 4095)
        return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(MmOperation::Protect);
    request.length  = 3;
    request.data[0] = address;
    request.data[1] = (size + 4095) & ~4095ULL;
    request.data[2] = protection;
    return virtualMemoryClient.call(request);
}

int detail::MappingClient::sync(uint64_t address, uint64_t size) {
    if (!size || size > UINT64_MAX - 4095)
        return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(MmOperation::Sync);
    request.length  = 2;
    request.data[0] = address;
    request.data[1] = (size + 4095) & ~4095ULL;
    return virtualMemoryClient.call(request);
}

uint64_t detail::MappingClient::openShared(const char* name, int flags) {
    if (!name || Runtime::stringLength(name) >= 64)
        return static_cast<int64_t>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(MmOperation::SharedOpen);
    request.length  = 9;
    request.data[0] = flags;
    Runtime::copyString(reinterpret_cast<char*>(&request.data[1]), name, 64);
    return virtualMemoryClient.call(request);
}

int detail::MappingClient::unlinkShared(const char* name) {
    if (!name || Runtime::stringLength(name) >= 64)
        return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type   = static_cast<uint64_t>(MmOperation::SharedUnlink);
    request.length = 9;
    Runtime::copyString(reinterpret_cast<char*>(&request.data[1]), name, 64);
    return virtualMemoryClient.call(request);
}

detail::MemoryControl::MemoryControl()
    : Sys::Transport(CoreServiceKind::Memory) {}

void Runtime::initializeHeap() { runtimeHeap.initialize(); }

}  // namespace os

namespace os {
uint64_t mapMemory(uint64_t address, uint64_t size, uint64_t protection) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Map);
    request.length  = 6;
    request.data[0] = address;
    request.data[1] = size;
    request.data[2] = protection;
    request.data[3] = 0;
    request.data[4] = 0;
    request.data[5] = 0;
    return services::RequestChannel(CoreServiceKind::Memory)
        .result(request, reply);
}
}  // namespace os

namespace os {
bool unmapMemory(uint64_t address, uint64_t size) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Unmap);
    request.length  = 5;
    request.data[0] = address;
    request.data[1] = size;
    request.data[2] = 0;
    return services::RequestChannel(CoreServiceKind::Memory)
               .result(request, reply) == 0;
}
}  // namespace os

namespace os {
bool protectMemory(uint64_t address, uint64_t size, uint64_t protection) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MemoryOperation::Protect);
    request.length  = 5;
    request.data[0] = address;
    request.data[1] = size;
    request.data[2] = protection;
    request.data[3] = 0;
    return services::RequestChannel(CoreServiceKind::Memory)
               .result(request, reply) == 0;
}
}  // namespace os

extern "C" void* __osinfo_runtime_allocate(size_t size) {
    return os::Runtime::allocateMemory(size);
}

extern "C" bool __osinfo_runtime_free(void* pointer) {
    return os::Runtime::freeMemory(pointer);
}

namespace os {
namespace {
int memoryError(ErrorCode value) { return static_cast<int>(value); }
bool mappingSize(uint64_t size, uint64_t& aligned) {
    if (!size || size > 32ULL * 1024 * 1024) return false;
    aligned = (size + 4095) & ~4095ULL;
    return true;
}
bool mappingProtection(uint32_t protection) {
    return protection <= 7 && (protection & 6) != 6;
}
int64_t memoryRequest(MemoryOperation operation, uint64_t first,
                      IpcMessage& reply, uint64_t second = 0) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(operation);
    request.length  = 5;
    request.data[0] = first;
    request.data[1] = second;
    return services::RequestChannel(CoreServiceKind::Memory)
        .result(request, reply);
}
}  // namespace
Mem::Mem() = default;
int Mem::refresh(uint64_t task) {
    wire::MemoryUsage value{};
    const int status = detail::MemoryQuery().usage(value, task);
    if (status) return status;
    if (value.free > value.total || value.owned > value.total || !value.nodes)
        return memoryError(ErrorCode::IoError);
    total_         = value.total;
    free_          = value.free;
    owned_         = value.owned;
    generation_    = value.generation;
    nodes_         = value.nodes;
    snapshotValid_ = true;
    return 0;
}
int64_t Mem::reclaimCaches() { return detail::MemoryControl().reclaimCaches(); }
uint64_t Mem::getTotal() const { return total_; }
uint64_t Mem::getFree() const { return free_; }
uint64_t Mem::getOwned() const { return owned_; }
uint64_t Mem::getGeneration() const { return generation_; }
uint32_t Mem::getNodeCount() const { return nodes_; }
bool Mem::getSnapshotValid() const { return snapshotValid_; }
Mem::Page::Page() = default;
Mem::Page::~Page() { close(); }
int Mem::Page::create(uint32_t order, uint16_t node, uint64_t owner) {
    if (handle_.getValid()) return error_ = memoryError(ErrorCode::Busy);
    if (order > 12) return error_ = memoryError(ErrorCode::InvalidArgument);
    const int64_t value = static_cast<int64_t>(
        detail::MemoryControl().allocatePages(order, node, owner));
    if (value <= 0)
        return error_ = value ? value : memoryError(ErrorCode::OutOfMemory);
    int status = handle_.adopt(value);
    if (!status) status = handle_.setFlags(handleFlagCloseOnExec);
    if (!status) status = refresh();
    if (status) {
        close();
        return error_ = status;
    }
    return error_ = 0;
}
int Mem::Page::open(Ipc::Handle&& handle) {
    if (handle_.getValid()) return error_ = memoryError(ErrorCode::Busy);
    if (!handle.getValid() || !handle.getOwned())
        return error_ = memoryError(ErrorCode::InvalidHandle);
    wire::PageObjectInfo value{};
    const int status = detail::MemoryQuery().pageInfo(handle.getValue(), value);
    if (status) return error_ = status;
    if (value.order > 12 || value.size != (4096ULL << value.order))
        return error_ = memoryError(ErrorCode::IoError);
    handle_       = static_cast<Ipc::Handle&&>(handle);
    owner_        = value.owner;
    size_         = value.size;
    references_   = value.references;
    node_         = value.node;
    order_        = value.order;
    return error_ = 0;
}
int Mem::Page::refresh() {
    if (!handle_.getValid())
        return error_ = memoryError(ErrorCode::InvalidHandle);
    wire::PageObjectInfo value{};
    const int status =
        detail::MemoryQuery().pageInfo(handle_.getValue(), value);
    if (status) return error_ = status;
    if (value.order > 12 || value.size != (4096ULL << value.order))
        return error_ = memoryError(ErrorCode::IoError);
    owner_        = value.owner;
    size_         = value.size;
    references_   = value.references;
    node_         = value.node;
    order_        = value.order;
    return error_ = 0;
}
int Mem::Page::close() {
    if (handle_.getValid()) {
        const int status = handle_.close();
        if (status) return error_ = status;
    }
    owner_ = size_ = references_ = node_ = order_ = 0;
    return error_                                 = 0;
}
uint64_t Mem::Page::getHandle() const { return handle_.getValue(); }
uint64_t Mem::Page::getOwner() const { return owner_; }
uint64_t Mem::Page::getSize() const { return size_; }
uint32_t Mem::Page::getReferences() const { return references_; }
uint16_t Mem::Page::getNode() const { return node_; }
uint16_t Mem::Page::getOrder() const { return order_; }
bool Mem::Page::getValid() const { return handle_.getValid(); }
int Mem::Page::getError() const { return error_; }
int detail::MappingClient::owned(uint64_t operation, uint64_t address,
                                 uint64_t size, uint64_t identity,
                                 uint64_t argument) {
    IpcMessage request{};
    request.type    = operation;
    request.length  = 5;
    request.data[0] = address;
    request.data[1] = size;
    request.data[2] = identity;
    request.data[3] = argument;
    return virtualMemoryClient.call(request);
}
Mem::Map::Map() = default;
Mem::Map::~Map() { close(); }
Mem::Map::Map(Map&& other) noexcept { *this = static_cast<Map&&>(other); }
Mem::Map& Mem::Map::operator=(Map&& other) noexcept {
    if (this == &other) return *this;
    if (address_ || backing_.getValid()) {
        error_ = memoryError(ErrorCode::Busy);
        return *this;
    }
    address_       = other.address_;
    size_          = other.size_;
    offset_        = other.offset_;
    identity_      = other.identity_;
    protection_    = other.protection_;
    shared_        = other.shared_;
    error_         = other.error_;
    backing_       = static_cast<Ipc::Handle&&>(other.backing_);
    other.address_ = other.size_ = other.offset_ = other.identity_ = 0;
    other.protection_                                              = 0;
    other.shared_                                                  = false;
    other.error_                                                   = 0;
    return *this;
}
int Mem::Map::bind(int64_t address, uint64_t identity, uint64_t size,
                   uint64_t offset, uint32_t protection, bool shared) {
    if (address <= 0)
        return error_ = address ? address : memoryError(ErrorCode::IoError);
    address_    = address;
    size_       = size;
    offset_     = offset;
    protection_ = protection;
    shared_     = shared;
    identity_   = identity;
    if ((address_ & 4095) || address_ < 0x10000 ||
        address_ >= 0x0000800000000000ULL ||
        size_ > 0x0000800000000000ULL - address_ || !identity_)
        return error_ = memoryError(ErrorCode::IoError);
    return error_ = 0;
}
int Mem::Map::create(uint64_t size, uint32_t protection, bool shared) {
    if (address_ || backing_.getValid())
        return error_ = memoryError(ErrorCode::Busy);
    uint64_t aligned;
    if (!mappingSize(size, aligned) || !mappingProtection(protection))
        return error_ = memoryError(ErrorCode::InvalidArgument);
    uint64_t identity = 0;
    int64_t address;
    if (shared) {
        IpcMessage reply{};
        const int64_t handle =
            memoryRequest(MemoryOperation::AnonymousShared, aligned, reply);
        if (handle <= 0)
            return error_ =
                       handle ? handle : memoryError(ErrorCode::OutOfMemory);
        int status = backing_.adopt(handle);
        if (!status) status = backing_.setFlags(handleFlagCloseOnExec);
        if (status) {
            backing_.close();
            return error_ = status;
        }
        address = static_cast<int64_t>(detail::MappingClient().shared(
            handle, aligned, protection, 0, 0, &identity));
    } else
        address = static_cast<int64_t>(detail::MappingClient().anonymous(
            aligned, protection, true, 0, &identity));
    const int status = bind(address, identity, aligned, 0, protection, shared);
    if (status && !address_) backing_.close();
    return status;
}
int Mem::Map::open(File& file, uint64_t size, uint64_t offset,
                   uint32_t protection, bool shared) {
    if (address_ || backing_.getValid())
        return error_ = memoryError(ErrorCode::Busy);
    uint64_t aligned;
    if (!file.getOpened() || file.getBackend() != File::Backend::Filesystem)
        return error_ = memoryError(ErrorCode::InvalidHandle);
    if (!mappingSize(size, aligned) || !mappingProtection(protection) ||
        (offset & 4095) || offset > UINT64_MAX - aligned)
        return error_ = memoryError(ErrorCode::InvalidArgument);
    if (shared) return error_ = memoryError(ErrorCode::NotSupported);
    uint64_t identity     = 0;
    const int64_t address = static_cast<int64_t>(detail::MappingClient().file(
        file.getHandle(), aligned, protection, offset, 0, &identity));
    return bind(address, identity, aligned, offset, protection, false);
}
int Mem::Map::open(Share& memory, uint64_t size, uint64_t offset,
                   uint32_t protection) {
    if (address_ || backing_.getValid())
        return error_ = memoryError(ErrorCode::Busy);
    uint64_t aligned;
    if (!memory.getOpened())
        return error_ = memoryError(ErrorCode::InvalidHandle);
    if (!mappingSize(size, aligned) || !mappingProtection(protection) ||
        (offset & 4095) || offset > UINT64_MAX - aligned)
        return error_ = memoryError(ErrorCode::InvalidArgument);
    uint64_t identity     = 0;
    const int64_t address = static_cast<int64_t>(detail::MappingClient().shared(
        memory.getHandle(), aligned, protection, offset, 0, &identity));
    return bind(address, identity, aligned, offset, protection, true);
}
int Mem::Map::close() {
    if (address_) {
        if (!identity_) return error_ = memoryError(ErrorCode::IoError);
        const int status = detail::MappingClient().owned(
            static_cast<uint64_t>(MmOperation::UnmapOwned), address_, size_,
            identity_);
        if (status) return error_ = status;
        address_ = size_ = offset_ = identity_ = 0;
        protection_                            = 0;
        shared_                                = false;
    }
    if (backing_.getValid()) {
        const int status = backing_.close();
        if (status) return error_ = status;
    }
    return error_ = 0;
}
uint64_t Mem::Map::release() {
    const uint64_t address = address_;
    address_ = size_ = offset_ = identity_ = 0;
    protection_                            = 0;
    shared_                                = false;
    error_ = backing_.getValid() ? backing_.close() : 0;
    return address;
}
int Mem::Map::sync(int mode) {
    if (!address_ || !identity_)
        return error_ = memoryError(ErrorCode::InvalidHandle);
    if (mode != MS_SYNC && mode != MS_ASYNC)
        return error_ = memoryError(mode & MS_INVALIDATE
                                        ? ErrorCode::NotSupported
                                        : ErrorCode::InvalidArgument);
    return error_ = detail::MappingClient().owned(
               static_cast<uint64_t>(MmOperation::SyncOwned), address_, size_,
               identity_, mode);
}
int Mem::Map::setProtection(uint32_t protection) {
    if (!address_ || !identity_)
        return error_ = memoryError(ErrorCode::InvalidHandle);
    if (!mappingProtection(protection))
        return error_ = memoryError(ErrorCode::InvalidArgument);
    const int status = detail::MappingClient().owned(
        static_cast<uint64_t>(MmOperation::ProtectOwned), address_, size_,
        identity_, protection);
    if (!status) protection_ = protection;
    return error_ = status;
}
uint64_t Mem::Map::getAddress() const { return address_; }
uint64_t Mem::Map::getSize() const { return size_; }
uint32_t Mem::Map::getProtection() const { return protection_; }
uint64_t Mem::Map::getOffset() const { return offset_; }
bool Mem::Map::getShared() const { return shared_; }
bool Mem::Map::getMapped() const { return address_ != 0; }
bool Mem::Map::getValid() const { return address_ && identity_; }
int Mem::Map::getError() const { return error_; }
Mem::Share::Share() = default;
Mem::Share::~Share() { close(); }
Mem::Share::Share(Share&& other) noexcept {
    *this = static_cast<Share&&>(other);
}
Mem::Share& Mem::Share::operator=(Share&& other) noexcept {
    if (this == &other) return *this;
    if (handle_.getValid()) {
        error_ = memoryError(ErrorCode::Busy);
        return *this;
    }
    handle_ = static_cast<Ipc::Handle&&>(other.handle_);
    Runtime::copyString(name_, other.name_, sizeof(name_));
    size_          = other.size_;
    error_         = other.error_;
    other.name_[0] = 0;
    other.size_    = 0;
    other.error_   = 0;
    return *this;
}
int Mem::Share::open(const char* name, int flags) {
    if (handle_.getValid()) return error_ = memoryError(ErrorCode::Busy);
    if (!name || name[0] != '/' || !name[1] ||
        Runtime::stringLength(name) >= sizeof(name_) ||
        (flags & ~(O_ACCMODE | O_CREAT | O_EXCL | O_CLOEXEC)) ||
        ((flags & O_ACCMODE) != O_RDONLY && (flags & O_ACCMODE) != O_RDWR) ||
        ((flags & O_EXCL) && !(flags & O_CREAT)))
        return error_ = memoryError(ErrorCode::InvalidArgument);
    for (const char* p = name + 1; *p; ++p)
        if (*p == '/') return error_ = memoryError(ErrorCode::InvalidArgument);
    const int64_t value =
        static_cast<int64_t>(detail::MappingClient().openShared(name, flags));
    if (value <= 0)
        return error_ = value ? value : memoryError(ErrorCode::NotFound);
    int status = handle_.adopt(value);
    Runtime::copyString(name_, name, sizeof(name_));
    if (!status) status = handle_.setFlags(handleFlagCloseOnExec);
    if (!status) status = refresh();
    if (status) {
        close();
        return error_ = status;
    }
    return error_ = 0;
}
int Mem::Share::close() {
    if (handle_.getValid()) {
        const int status = handle_.close();
        if (status) return error_ = status;
    }
    name_[0]      = 0;
    size_         = 0;
    return error_ = 0;
}
int Mem::Share::refresh() {
    if (!handle_.getValid())
        return error_ = memoryError(ErrorCode::InvalidHandle);
    IpcMessage reply{};
    const int status =
        memoryRequest(MemoryOperation::SharedInfo, handle_.getValue(), reply);
    if (status) return error_ = status;
    if (reply.length < 2) return error_ = memoryError(ErrorCode::IoError);
    size_         = reply.data[1];
    return error_ = 0;
}
int Mem::Share::resize(uint64_t size) {
    if (!handle_.getValid())
        return error_ = memoryError(ErrorCode::InvalidHandle);
    const int status =
        detail::MemoryControl().resizeSharedMemory(handle_.getValue(), size);
    if (!status) size_ = size;
    return error_ = status;
}
int Mem::Share::unlink() {
    if (!handle_.getValid())
        return error_ = memoryError(ErrorCode::InvalidHandle);
    IpcMessage reply{};
    return error_ = memoryRequest(MemoryOperation::UnlinkSharedHandle,
                                  handle_.getValue(), reply);
}
const char* Mem::Share::getName() const { return name_; }
uint64_t Mem::Share::getSize() const { return size_; }
uint64_t Mem::Share::getHandle() const { return handle_.getValue(); }
bool Mem::Share::getOpened() const { return handle_.getValid(); }
bool Mem::Share::getValid() const { return handle_.getValid(); }
int Mem::Share::getError() const { return error_; }
}  // namespace os
