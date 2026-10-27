#include "service/common/channel.h"
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include <os/mm.h>
#include <os/service.h>

namespace os {
namespace {
constexpr uint64_t magic          = 0x5356435354415445ULL,
                   maximumBytes   = 4ULL * 1024 * 1024;
constexpr uint64_t logSchema      = 0x4c4f475245434f52ULL;
constexpr uint64_t stagingAddress = 0x6a000000, snapshotAddress = 0x6b000000;

struct Header {
    uint64_t magic, schema, size, checksum;
};
static_assert(sizeof(Header) == 32);
}  // namespace

int Sys::Checkpoint::openSnapshot(void* state, uint64_t bytes,
                                  uint64_t schema) {
    if (!bytes || bytes > maximumBytes - sizeof(Header)) return -1;
    IpcMessage reply{};
    const uint64_t deadline = Sys().readMonotonicTime() + 20000000000ULL;
    int status;
    while ((status = recovery_.acquire(reply)) ==
           static_cast<int>(ErrorCode::WouldBlock)) {
        if (Sys().readMonotonicTime() >= deadline)
            return static_cast<int>(ErrorCode::Timeout);
        sleep(1);
    }
    if (status) return status;
    epoch_        = reply.data[1];
    sequence_     = reply.data[2];
    bytes_        = bytes;
    schema_       = schema;
    baseSequence_ = reply.data[4];
    records_      = reply.data[5];
    if (records_ > 128 || baseSequence_ > sequence_ ||
        sequence_ - baseSequence_ != records_) {
        for (uint32_t i = 0; i < reply.handleCount; ++i)
            closeHandle(reply.handles[i]);
        return -1;
    }
    if (!reply.data[3]) return 0;
    if (reply.handleCount != 1) return -1;
    const uint64_t handle = reply.handles[0], total = reply.data[3],
                   mapped = (total + 4095) & ~4095ULL;
    if (total != sizeof(Header) + bytes || mapped > maximumBytes) {
        closeHandle(handle);
        return -1;
    }
    if (detail::MemoryControl().mapPages(handle, snapshotAddress, mapped, 0,
                                         0) != snapshotAddress) {
        closeHandle(handle);
        return -5;
    }
    const auto* header  = reinterpret_cast<const Header*>(snapshotAddress);
    const void* payload = header + 1;
    const bool valid    = header->magic == magic && header->schema == schema &&
                       header->size == bytes &&
                       header->checksum == checksum(payload, bytes);
    if (valid) Runtime::copyBytes(state, payload, bytes);
    const int unmapped =
        detail::MemoryControl().unmapTarget(snapshotAddress, mapped);
    closeHandle(handle);
    return valid && !unmapped ? 1 : -1;
}

int Sys::Checkpoint::prepare() {
    const uint64_t total = sizeof(Header) + bytes_;
    const int status     = ensureStaging(total);
    if (status) return status;
    return recovery_.prepare(total, false);
}

int Sys::Checkpoint::commit(const void* state) {
    const int status = publish(state, bytes_, false);
    if (!status) {
        baseSequence_ = sequence_;
        records_      = 0;
    }
    return status;
}

int Sys::Checkpoint::prepareLog(uint64_t bytes) {
    if (!bytes || bytes > 64 * 1024 - sizeof(Header)) return -1;
    const auto total = sizeof(Header) + bytes;
    const int status = ensureStaging(
        total > sizeof(Header) + bytes_ ? total : sizeof(Header) + bytes_);
    if (status) return status;
    return recovery_.prepare(total, true);
}

int Sys::Checkpoint::append(const void* record, uint64_t bytes) {
    const int status = publish(record, bytes, true);
    if (!status) ++records_;
    return status;
}

int Sys::Checkpoint::readLog(uint32_t index, void* record, uint64_t bytes) {
    if (index >= records_) return -1;
    IpcMessage reply{};
    const int status = recovery_.readLog(index, reply);
    if (status) return status;
    if (reply.handleCount != 1) return -1;
    const uint64_t handle = reply.handles[0], total = reply.data[1],
                   mapped = (total + 4095) & ~4095ULL;
    if (total != sizeof(Header) + bytes || mapped > maximumBytes ||
        reply.data[2] != baseSequence_ + index + 1) {
        closeHandle(handle);
        return -1;
    }
    if (detail::MemoryControl().mapPages(handle, snapshotAddress, mapped, 0,
                                         0) != snapshotAddress) {
        closeHandle(handle);
        return -5;
    }
    const auto* header = reinterpret_cast<const Header*>(snapshotAddress);
    const bool valid   = header->magic == magic &&
                       header->schema == (schema_ ^ logSchema) &&
                       header->size == bytes &&
                       header->checksum == checksum(header + 1, bytes);
    if (valid) Runtime::copyBytes(record, header + 1, bytes);
    const int unmapped =
        detail::MemoryControl().unmapTarget(snapshotAddress, mapped);
    closeHandle(handle);
    return valid && !unmapped ? 0 : -1;
}

uint64_t Sys::Checkpoint::put(uint64_t handle) {
    IpcMessage reply{};
    return !call(TaskOperation::RecoveryPut, handle, 0, 0, reply)
               ? reply.data[1]
               : 0;
}

uint64_t Sys::Checkpoint::get(uint64_t token) {
    IpcMessage reply{};
    return !call(TaskOperation::RecoveryGet, token, 0, 0, reply) &&
                   reply.handleCount == 1
               ? reply.data[1]
               : 0;
}

bool Sys::Checkpoint::drop(uint64_t token) {
    IpcMessage reply{};
    return !call(TaskOperation::RecoveryDrop, token, sequence_, 0, reply);
}

uint64_t Sys::Checkpoint::next(uint64_t& cursor) {
    IpcMessage reply{};
    if (call(TaskOperation::RecoveryList, cursor, 0, 0, reply)) return 0;
    cursor = reply.data[2];
    return reply.data[1];
}

uint64_t Sys::Checkpoint::getSequence() const { return sequence_; }

uint64_t Sys::Checkpoint::getEpoch() const { return epoch_; }

uint32_t Sys::Checkpoint::getRecordCount() const { return records_; }

int Sys::Checkpoint::queryGeneration(uint64_t task, uint64_t& value) {
    IpcMessage reply{};
    const int status =
        call(TaskOperation::RecoveryGeneration, task, 0, 0, reply);
    if (!status) value = reply.data[1];
    return status;
}

int Sys::Checkpoint::ensureStaging(uint64_t total) {
    if (!epoch_ || !total || total > maximumBytes) return -1;
    if (staging_ && capacity_ >= total) return 0;
    if (staging_) {
        const int status =
            detail::MemoryControl().unmapTarget(stagingAddress, capacity_);
        if (status) return status;
        closeHandle(staging_);
        staging_ = capacity_ = 0;
    }
    uint32_t order = 0;
    while ((4096ULL << order) < total) ++order;
    const int64_t handle = detail::MemoryControl().allocatePages(order);
    if (handle <= 0) return handle ? handle : -5;
    const uint64_t mapped = 4096ULL << order;
    if (detail::MemoryControl().mapPages(handle, stagingAddress, mapped, 0,
                                         1) != stagingAddress) {
        closeHandle(handle);
        return -5;
    }
    staging_  = handle;
    capacity_ = mapped;
    return 0;
}

int Sys::Checkpoint::publish(const void* state, uint64_t bytes, bool append) {
    if (!staging_ || !state || !bytes || bytes > capacity_ - sizeof(Header))
        return -1;
    auto* header = reinterpret_cast<Header*>(stagingAddress);
    Runtime::copyBytes(header + 1, state, bytes);
    *header = {magic, append ? schema_ ^ logSchema : schema_, bytes,
               checksum(header + 1, bytes)};
    IpcMessage reply{};
    const int status = append
                           ? recovery_.append(staging_, sizeof(Header) + bytes,
                                              sequence_, reply)
                           : recovery_.commit(staging_, sizeof(Header) + bytes,
                                              sequence_, reply);
    if (!status) sequence_ = reply.data[1];
    return status;
}

uint64_t Sys::Checkpoint::checksum(const void* data, uint64_t size) {
    uint64_t hash     = 14695981039346656037ULL;
    const auto* input = static_cast<const uint8_t*>(data);
    for (uint64_t i = 0; i < size; ++i) {
        hash ^= input[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int Sys::Checkpoint::call(TaskOperation op, uint64_t a, uint64_t b, uint64_t c,
                          IpcMessage& reply) {
    IpcMessage request{};
    request.type    = static_cast<uint64_t>(op);
    request.length  = 5;
    request.data[0] = epoch_;
    request.data[1] = a;
    request.data[2] = b;
    request.data[3] = c;
    return services::RequestChannel(CoreServiceKind::Task)
        .result(request, reply);
}

}  // namespace os

namespace os {
Sys::Checkpoint::Checkpoint() = default;
Sys::Checkpoint::~Checkpoint() { close(); }
int Sys::Checkpoint::open(void* state, uint64_t bytes, uint64_t schema) {
    if (opened_ || staging_) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!state) return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    const int result = openSnapshot(state, bytes, schema);
    if (result >= 0) {
        opened_   = true;
        restored_ = result > 0;
    }
    error_ = result < 0 ? result : 0;
    return result;
}
int Sys::Checkpoint::close() {
    if (staging_ && capacity_) {
        const int result =
            detail::MemoryControl().unmapTarget(stagingAddress, capacity_);
        if (result) return error_ = result;
        capacity_ = 0;
    }
    if (staging_) {
        Ipc::Handle handle(staging_, false);
        const int result = handle.close();
        if (handle.getValid()) return error_ = result;
        staging_ = 0;
    }
    opened_ = restored_ = false;
    epoch_ = sequence_ = bytes_ = schema_ = baseSequence_ = 0;
    records_                                              = 0;
    return error_                                         = 0;
}
bool Sys::Checkpoint::getValid() const { return opened_; }
int Sys::Checkpoint::getError() const { return error_; }
bool Sys::Checkpoint::getRestored() const { return restored_; }
}  // namespace os
