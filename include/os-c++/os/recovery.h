#pragma once
#include "os/core_service.h"
#include "os/mm.h"

namespace os {
class ServiceCheckpoint {
   public:
    // Positive means an existing checkpoint was restored; zero means a fresh domain.
    int open(void* state, uint64_t bytes, uint64_t schema) {
        if (!bytes || bytes > maximumBytes - sizeof(Header)) return -1;
        IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(TaskOperation::RecoveryAcquire); request.length = 5;
        const uint64_t deadline = Clock::getMonotonicTime() + 20000000000ULL;
        int status;
        while ((status = CoreServiceClient::result(CoreServiceKind::Task, request, reply)) == static_cast<int>(ErrorCode::WouldBlock)) {
            if (Clock::getMonotonicTime() >= deadline) return static_cast<int>(ErrorCode::Timeout);
            sleep(1);
        }
        if (status) return status;
        epoch_ = reply.data[1]; sequence_ = reply.data[2]; bytes_ = bytes; schema_ = schema;
        baseSequence_ = reply.data[4]; records_ = reply.data[5];
        if (records_ > 128 || baseSequence_ > sequence_ || sequence_ - baseSequence_ != records_) {
            for (uint32_t i = 0; i < reply.handleCount; ++i) closeHandle(reply.handles[i]);
            return -1;
        }
        if (!reply.data[3]) return 0;
        if (reply.handleCount != 1) return -1;
        const uint64_t handle = reply.handles[0], total = reply.data[3], mapped = (total + 4095) & ~4095ULL;
        if (total != sizeof(Header) + bytes || mapped > maximumBytes) { closeHandle(handle); return -1; }
        if (Memory::mapPages(handle, snapshotAddress, mapped, 0, 0) != snapshotAddress) { closeHandle(handle); return -5; }
        const auto* header = reinterpret_cast<const Header*>(snapshotAddress);
        const void* payload = header + 1;
        const bool valid = header->magic == magic && header->schema == schema && header->size == bytes && header->checksum == checksum(payload, bytes);
        if (valid) Runtime::copyBytes(state, payload, bytes);
        const int unmapped = Memory::unmapTarget(snapshotAddress, mapped);
        closeHandle(handle);
        return valid && !unmapped ? 1 : -1;
    }
    int prepare() {
        const uint64_t total = sizeof(Header) + bytes_;
        const int status = ensureStaging(total); if (status) return status;
        IpcMessage reply{}; return call(TaskOperation::RecoveryPrepare, total, 0, 0, reply);
    }
    int commit(const void* state) {
        const int status = publish(state, bytes_, false);
        if (!status) { baseSequence_ = sequence_; records_ = 0; }
        return status;
    }
    // WouldBlock requires compaction into a full checkpoint before any mutation.
    int prepareLog(uint64_t bytes) {
        if (!bytes || bytes > 64 * 1024 - sizeof(Header)) return -1;
        const auto total = sizeof(Header) + bytes;
        const int status = ensureStaging(total > sizeof(Header) + bytes_ ? total : sizeof(Header) + bytes_);
        if (status) return status;
        IpcMessage reply{}; return call(TaskOperation::RecoveryPrepare, total, 1, 0, reply);
    }
    int append(const void* record, uint64_t bytes) {
        const int status = publish(record, bytes, true);
        if (!status) ++records_;
        return status;
    }
    int readLog(uint32_t index, void* record, uint64_t bytes) {
        if (index >= records_) return -1;
        IpcMessage reply{};
        const int status = call(TaskOperation::RecoveryReadLog, index, 0, 0, reply);
        if (status) return status;
        if (reply.handleCount != 1) return -1;
        const uint64_t handle = reply.handles[0], total = reply.data[1], mapped = (total + 4095) & ~4095ULL;
        if (total != sizeof(Header) + bytes || mapped > maximumBytes || reply.data[2] != baseSequence_ + index + 1) { closeHandle(handle); return -1; }
        if (Memory::mapPages(handle, snapshotAddress, mapped, 0, 0) != snapshotAddress) { closeHandle(handle); return -5; }
        const auto* header = reinterpret_cast<const Header*>(snapshotAddress);
        const bool valid = header->magic == magic && header->schema == (schema_ ^ logSchema) &&
            header->size == bytes && header->checksum == checksum(header + 1, bytes);
        if (valid) Runtime::copyBytes(record, header + 1, bytes);
        const int unmapped = Memory::unmapTarget(snapshotAddress, mapped); closeHandle(handle);
        return valid && !unmapped ? 0 : -1;
    }
    uint64_t put(uint64_t handle) {
        IpcMessage reply{};
        return !call(TaskOperation::RecoveryPut, handle, 0, 0, reply) ? reply.data[1] : 0;
    }
    uint64_t get(uint64_t token) {
        IpcMessage reply{};
        return !call(TaskOperation::RecoveryGet, token, 0, 0, reply) && reply.handleCount == 1 ? reply.data[1] : 0;
    }
    bool drop(uint64_t token) {
        IpcMessage reply{}; return !call(TaskOperation::RecoveryDrop, token, sequence_, 0, reply);
    }
    uint64_t next(uint64_t& cursor) {
        IpcMessage reply{};
        if (call(TaskOperation::RecoveryList, cursor, 0, 0, reply)) return 0;
        cursor = reply.data[2]; return reply.data[1];
    }
    uint64_t sequence() const { return sequence_; }
    uint64_t epoch() const { return epoch_; }
    uint32_t records() const { return records_; }
    int generation(uint64_t task, uint64_t& value) {
        IpcMessage reply{};
        const int status = call(TaskOperation::RecoveryGeneration, task, 0, 0, reply);
        if (!status) value = reply.data[1];
        return status;
    }
   private:
    struct Header { uint64_t magic, schema, size, checksum; };
    static constexpr uint64_t magic = 0x5356435354415445ULL, maximumBytes = 4ULL * 1024 * 1024;
    static constexpr uint64_t logSchema = 0x4c4f475245434f52ULL;
    static constexpr uint64_t stagingAddress = 0x6a000000, snapshotAddress = 0x6b000000;
    uint64_t epoch_ = 0, sequence_ = 0, bytes_ = 0, schema_ = 0, staging_ = 0, capacity_ = 0;
    uint64_t baseSequence_ = 0;
    uint32_t records_ = 0;
    int ensureStaging(uint64_t total) {
        if (!epoch_ || !total || total > maximumBytes) return -1;
        if (staging_ && capacity_ >= total) return 0;
        if (staging_) {
            const int status = Memory::unmapTarget(stagingAddress, capacity_);
            if (status) return status;
            closeHandle(staging_); staging_ = capacity_ = 0;
        }
        uint32_t order = 0; while ((4096ULL << order) < total) ++order;
        const int64_t handle = Memory::allocatePages(order);
        if (handle <= 0) return handle ? handle : -5;
        const uint64_t mapped = 4096ULL << order;
        if (Memory::mapPages(handle, stagingAddress, mapped, 0, 1) != stagingAddress) { closeHandle(handle); return -5; }
        staging_ = handle; capacity_ = mapped; return 0;
    }
    int publish(const void* state, uint64_t bytes, bool append) {
        if (!staging_ || !state || !bytes || bytes > capacity_ - sizeof(Header)) return -1;
        auto* header = reinterpret_cast<Header*>(stagingAddress);
        Runtime::copyBytes(header + 1, state, bytes);
        *header = {magic, append ? schema_ ^ logSchema : schema_, bytes, checksum(header + 1, bytes)};
        IpcMessage reply{};
        const int status = call(append ? TaskOperation::RecoveryAppend : TaskOperation::RecoveryCommit,
                                staging_, sizeof(Header) + bytes, sequence_, reply);
        if (!status) sequence_ = reply.data[1];
        return status;
    }
    static uint64_t checksum(const void* data, uint64_t size) {
        uint64_t hash = 14695981039346656037ULL;
        const auto* input = static_cast<const uint8_t*>(data);
        for (uint64_t i = 0; i < size; ++i) { hash ^= input[i]; hash *= 1099511628211ULL; }
        return hash;
    }
    int call(TaskOperation op, uint64_t a, uint64_t b, uint64_t c, IpcMessage& reply) {
        IpcMessage request{}; request.type = static_cast<uint64_t>(op); request.length = 5;
        request.data[0] = epoch_; request.data[1] = a; request.data[2] = b; request.data[3] = c;
        return CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    }
};
}
