#pragma once
#include "recovery.h"

namespace services {
class ReplayCache {
 public:
    static constexpr uint32_t capacity = 128, noRecord = capacity;
    struct Record {
        uint64_t task = 0, thread = 0, generation = 0, id = 0, digest = 0;
        uint32_t length = 0, handleCount = 0;
        uint64_t words[65]{}, tokens[2]{};
        uint32_t rights[2]{}, options[2]{};
        bool complete = false;
    } records[capacity];
    struct Request { uint64_t id = 0; int resumeError = 0; uint32_t slot = noRecord; };
    // Signal handlers may issue nested RPCs on the same authenticated thread.
    int begin(os::IpcMessage& message, Request& request, os::ServiceCheckpoint& checkpoint) {
        if (message.length < 3 || message.data[message.length - 3] != os::serviceReplayMagic) return 0;
        request.id = message.data[message.length - 2]; request.resumeError = static_cast<int64_t>(message.data[message.length - 1]);
        message.length -= 3;
        if (!request.id || (request.resumeError && request.resumeError != static_cast<int>(os::ErrorCode::Interrupted) &&
            request.resumeError != static_cast<int>(os::ErrorCode::Timeout))) return -1;
        uint64_t generation = 0;
        const int status = checkpoint.generation(message.senderPid, generation); if (status) return status;
        const auto hash = digest(message);
        uint32_t empty = noRecord;
        for (uint32_t i = 0; i < capacity; ++i) {
            auto& record = records[i];
            if (!record.task && empty == noRecord) empty = i;
            if (record.task != message.senderPid || record.thread != message.senderTid || record.generation != generation) continue;
            if (record.id == request.id) {
                if (record.digest != hash) return -1;
                request.slot = i;
                return record.complete ? 1 : request.resumeError;
            }
        }
        if (request.resumeError) return request.resumeError;
        if (empty == noRecord) return static_cast<int>(os::ErrorCode::OutOfMemory);
        auto& record = records[empty]; record = {};
        record.task = message.senderPid; record.thread = message.senderTid; record.generation = generation;
        record.id = request.id; record.digest = hash; request.slot = empty; return 0;
    }
    bool remember(uint32_t slot, const os::IpcMessage& reply, RecoveryCapabilities& capabilities) {
        if (slot == noRecord) return true;
        if (slot >= capacity || reply.length > 65 || reply.handleCount > 2) return false;
        auto& record = records[slot]; record.length = reply.length; record.handleCount = reply.handleCount;
        os::Runtime::copyBytes(record.words, reply.data, reply.length * sizeof(uint64_t));
        for (uint32_t i = 0; i < reply.handleCount; ++i) {
            record.tokens[i] = capabilities.encode(reply.handles[i]); if (!record.tokens[i]) return false;
            record.rights[i] = reply.handleRights[i]; record.options[i] = reply.handleOptions[i];
        }
        record.complete = true; return true;
    }
    bool restoreReply(uint32_t slot, os::ServiceCheckpoint& checkpoint, os::IpcMessage& reply) const {
        if (slot >= capacity || !records[slot].complete) return false;
        const auto& record = records[slot];
        reply = {}; reply.length = record.length;
        os::Runtime::copyBytes(reply.data, record.words, record.length * sizeof(uint64_t));
        for (uint32_t i = 0; i < record.handleCount; ++i) {
            const auto handle = checkpoint.get(record.tokens[i]);
            if (!handle) { for (uint32_t j = 0; j < reply.handleCount; ++j) os::closeHandle(reply.handles[j]); reply = {}; return false; }
            reply.handles[reply.handleCount++] = handle;
            reply.handleRights[i] = record.rights[i]; reply.handleOptions[i] = record.options[i];
        }
        return true;
    }
    uint32_t acknowledge(const os::IpcMessage& message, os::ServiceCheckpoint& checkpoint) {
        if (message.length != 2 || !message.data[0] || !message.data[1]) return noRecord;
        for (uint32_t i = 0; i < capacity; ++i) {
            auto& record = records[i];
            if (record.task == message.senderPid && record.thread == message.data[1] && record.id == message.data[0]) {
                uint64_t generation = 0;
                if (!checkpoint.generation(message.senderPid, generation) && record.generation == generation) return i;
            }
        }
        return noRecord;
    }
    bool retain(RecoveryCapabilities& capabilities) const {
        for (const auto& record : records) for (uint32_t i = 0; i < record.handleCount; ++i)
            if (!capabilities.keep(record.tokens[i])) return false;
        return true;
    }
    bool references(uint64_t token) const {
        for (const auto& record : records) for (uint32_t i = 0; i < record.handleCount; ++i) if (record.tokens[i] == token) return true;
        return false;
    }
    bool valid() const {
        for (const auto& record : records) if (record.task && (!record.id || !record.thread || !record.generation ||
            record.length > 65 || record.handleCount > 2 || (record.complete && !record.length))) return false;
        return true;
    }
 private:
    static uint64_t digest(const os::IpcMessage& request) {
        uint64_t value = request.type ^ (static_cast<uint64_t>(request.length) << 32);
        for (uint32_t i = 0; i < request.length; ++i) value = (value ^ request.data[i]) * 1099511628211ULL;
        for (uint32_t i = 0; i < request.handleCount; ++i) {
            value = (value ^ os::Handle::identity(request.handles[i])) * 1099511628211ULL;
            value = (value ^ request.handleRights[i]) * 1099511628211ULL;
        }
        return value;
    }
};
}
