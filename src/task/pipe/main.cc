#include "service/common/queries.h"
#include "service/common/runtime.h"
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>
#include <os/service.h>
#include <os/task.h>
#include "service/common/replay.h"

class PipeServer {
    static constexpr size_t capacity = 64, pendingCapacity = 128, bufferSize = 16384;
    struct Stream { unsigned char bytes[bufferSize]{}; uint32_t head = 0, count = 0, eof = 0; };
    struct End { uint64_t handle = 0, id = 0, token = 0; bool nonblocking = false; };
    struct Pair { bool used = false, channel = false; End ends[2]; Stream streams[2]; } pairs_[capacity];
    struct Pending {
        bool used = false; uint32_t pair = 0, end = 0, size = 0;
        os::PipeOperation operation{}; uint64_t token = 0, handle = 0;
        uint32_t outcome = services::ReplayCache::noRecord;
        unsigned char data[512]{};
    } pending_[pendingCapacity];
    uint64_t endpoint_ = 0;
    os::Sys::Checkpoint checkpoint_;
    services::RecoveryCapabilities capabilities_{checkpoint_};
    services::ReplayCache results_;
    bool durableResults_[services::ReplayCache::capacity]{};
    struct Snapshot { uint64_t endpoint = 0; Pair pairs[capacity]; services::ReplayCache results; } snapshot_;
    struct PairState {
        bool used = false, channel = false; End ends[2];
        struct Cursor { uint32_t head = 0, count = 0, eof = 0; } streams[2];
    };
    struct Delta {
        uint32_t pair = capacity, outcome = services::ReplayCache::noRecord;
        PairState value;
        services::ReplayCache::Record result;
        uint32_t stream = 2, offset = 0, size = 0;
        unsigned char bytes[512]{};
    } delta_;
    static_assert(sizeof(Delta) <= 4096 - 32);
    uint64_t nextCollection_ = 0;

    [[noreturn]] static void failed() { os::Sys().log("[PIPE] recovery transaction failed\n"); os::exitTask(2); }
    bool retain() {
        capabilities_.begin();
        if (!capabilities_.encode(endpoint_)) return false;
        for (auto& pair : pairs_) if (pair.used) for (auto& end : pair.ends) {
            end.token = capabilities_.encode(end.handle); if (!end.token) return false;
        }
        return results_.retain(capabilities_);
    }
    static void serialize(Pair& value, const Pair& pair) {
        if (!pair.used) { os::Runtime::clearMemory(&value, sizeof(value)); return; }
        value = pair;
        for (auto& end : value.ends) end.handle = end.token;
    }
    bool save() {
        if (!retain()) return false;
        snapshot_.endpoint = capabilities_.encode(endpoint_); snapshot_.results = results_;
        for (uint32_t i = 0; i < capacity; ++i) serialize(snapshot_.pairs[i], pairs_[i]);
        if (checkpoint_.commit(&snapshot_)) return false;
        for (uint32_t i = 0; i < services::ReplayCache::capacity; ++i) durableResults_[i] = results_.records[i].task != 0;
        return capabilities_.collect();
    }
    bool prepare() {
        int status = checkpoint_.prepareLog(sizeof(delta_));
        if (status == static_cast<int>(os::ErrorCode::WouldBlock)) {
            if (checkpoint_.prepare() || !save()) return false;
            status = checkpoint_.prepareLog(sizeof(delta_));
        }
        return !status;
    }
    void commit(uint32_t pair, uint32_t outcome, const Pending* request = nullptr) {
        if (!retain()) failed();
        os::Runtime::clearMemory(&delta_, sizeof(delta_)); delta_.pair = pair; delta_.outcome = outcome;
        if (pair < capacity && pairs_[pair].used) {
            const auto& source = pairs_[pair]; auto& target = delta_.value;
            target.used = true; target.channel = source.channel;
            for (uint32_t i = 0; i < 2; ++i) {
                target.ends[i] = source.ends[i]; target.ends[i].handle = target.ends[i].token;
                target.streams[i] = {source.streams[i].head, source.streams[i].count, source.streams[i].eof};
            }
            if (request && request->operation == os::PipeOperation::Write) {
                delta_.stream = source.channel ? 1 - request->end : 0;
                const auto& stream = source.streams[delta_.stream]; delta_.size = request->size;
                delta_.offset = (stream.head + stream.count - request->size) % bufferSize;
                os::Runtime::copyBytes(delta_.bytes, request->data, request->size);
            }
        }
        if (outcome < services::ReplayCache::capacity) delta_.result = results_.records[outcome];
        if (checkpoint_.append(&delta_, sizeof(delta_))) failed();
        if (outcome < services::ReplayCache::capacity) durableResults_[outcome] = results_.records[outcome].task != 0;
        if (!capabilities_.collect()) failed();
    }
    bool restore() {
        const int loaded = checkpoint_.open(&snapshot_, sizeof(snapshot_), 0x5049504553543032ULL);
        if (loaded < 0) return false;
        if (!loaded) {
            endpoint_ = os::detail::EndpointControl().create();
            return endpoint_ && !checkpoint_.prepare() && save();
        }
        for (uint32_t i = 0; i < checkpoint_.getRecordCount(); ++i) {
            if (checkpoint_.readLog(i, &delta_, sizeof(delta_)) || delta_.pair > capacity || delta_.outcome > services::ReplayCache::capacity) return false;
            if (delta_.size > 512 || (delta_.size && (delta_.pair >= capacity || !delta_.value.used || delta_.stream >= 2 || delta_.offset >= bufferSize))) return false;
            if (delta_.pair < capacity) {
                auto& target = snapshot_.pairs[delta_.pair]; const auto& source = delta_.value;
                if (!source.used) os::Runtime::clearMemory(&target, sizeof(target));
                else {
                    target.used = true; target.channel = source.channel;
                    for (uint32_t n = 0; n < 2; ++n) {
                        target.ends[n] = source.ends[n];
                        target.streams[n].head = source.streams[n].head;
                        target.streams[n].count = source.streams[n].count;
                        target.streams[n].eof = source.streams[n].eof;
                    }
                    if (delta_.size) for (uint32_t n = 0; n < delta_.size; ++n)
                        target.streams[delta_.stream].bytes[(delta_.offset + n) % bufferSize] = delta_.bytes[n];
                }
            }
            if (delta_.outcome < services::ReplayCache::capacity) snapshot_.results.records[delta_.outcome] = delta_.result;
        }
        if (!snapshot_.results.valid()) return false;
        results_ = snapshot_.results; capabilities_.begin(); endpoint_ = capabilities_.decode(snapshot_.endpoint);
        if (!endpoint_) return false;
        for (uint32_t i = 0; i < services::ReplayCache::capacity; ++i) durableResults_[i] = results_.records[i].task != 0;
        // Incomplete requests have no committed side effects and will be retried.
        for (auto& record : results_.records) if (!record.complete) record = {};
        for (uint32_t i = 0; i < capacity; ++i) {
            pairs_[i] = snapshot_.pairs[i]; auto& pair = pairs_[i];
            if (!pair.used) continue;
            for (const auto& stream : pair.streams) if (stream.head >= bufferSize || stream.count > bufferSize) return false;
            for (auto& end : pair.ends) {
                if (!end.handle || end.handle != end.token) return false;
                end.handle = capabilities_.decode(end.token);
                if (!end.handle || os::Ipc::Handle(end.handle, false).queryIdentity() != end.id) return false;
            }
            if (pair.ends[0].id == pair.ends[1].id) return false;
            for (uint32_t n = 0; n < i; ++n) if (pairs_[n].used)
                for (const auto& previous : pairs_[n].ends) for (const auto& end : pair.ends)
                    if (previous.id == end.id) return false;
        }
        for (const auto& record : results_.records) for (uint32_t n = 0; n < record.handleCount; ++n) {
            bool found = false;
            for (const auto& pair : pairs_) if (pair.used) for (const auto& end : pair.ends) if (end.token == record.tokens[n]) found = true;
            if (!found) return false;
        }
        if (!results_.retain(capabilities_) || !capabilities_.discardOrphans()) return false;
        os::Sys().log("[PIPE] byte streams and replay results restored\n"); return true;
    }

    static void closeTag(uint64_t handle) { if (handle) { os::Ipc::Handle(handle, false).destroyEndpoint(); os::closeHandle(handle); } }
    void release(size_t index) {
        auto& pair = pairs_[index];
        if (!prepare()) return;
        pair.used = false;
        commit(index, services::ReplayCache::noRecord);
        for (auto& end : pair.ends) closeTag(end.handle);
        pair = {};
    }
    bool live(const Pair& pair, size_t end) { return os::Ipc::Handle(pair.ends[end].handle, false).queryReferences() > 1; }
    bool reply(uint64_t token, int64_t status, os::IpcMessage* value = nullptr) {
        os::IpcMessage empty{}; auto& result = value ? *value : empty;
        result.data[0] = status; if (!result.length) result.length = 1;
        return os::replyMessage(endpoint_, token, result) == 0;
    }
    void finish(Pending& request, int64_t status, os::IpcMessage* value = nullptr) {
        os::IpcMessage empty{}; auto& result = value ? *value : empty;
        result.data[0] = status; if (!result.length) result.length = 1;

        if (!results_.remember(request.outcome, result, capabilities_)) failed();
        commit(request.pair, request.outcome, &request);

        os::replyMessage(endpoint_, request.token, result);
    }
    bool attempt(Pending& request) {
        auto& pair = pairs_[request.pair]; const auto end = request.end;
        auto& self = pair.ends[end];
        if (request.operation == os::PipeOperation::Read) {
            auto& stream = pair.streams[pair.channel ? end : 0];
            if (!stream.count && !stream.eof && live(pair, 1 - end)) {
                if (!self.nonblocking) return false;
                reply(request.token, -6); return true;
            }
            const uint32_t count = stream.count < request.size ? stream.count : request.size;
            if (!prepare()) { reply(request.token, -5); return true; }
            os::IpcMessage result{}; result.length = 1 + (count + 7) / 8;
            auto* data = reinterpret_cast<unsigned char*>(&result.data[1]);
            for (uint32_t i = 0; i < count; ++i) data[i] = stream.bytes[(stream.head + i) % bufferSize];
            stream.head = (stream.head + count) % bufferSize; stream.count -= count;
            if (!count && stream.eof) --stream.eof;
            finish(request, count, &result);
            return true;
        }
        auto& stream = pair.streams[pair.channel ? 1 - end : 0];
        if (!live(pair, 1 - end)) { reply(request.token, -13); return true; }
        if (bufferSize - stream.count < request.size) {
            if (!self.nonblocking) return false;
            reply(request.token, -6); return true;
        }
        if (!prepare()) { reply(request.token, -5); return true; }
        for (uint32_t i = 0; i < request.size; ++i) stream.bytes[(stream.head + stream.count + i) % bufferSize] = request.data[i];
        stream.count += request.size;
        finish(request, request.size);
        return true;
    }
    void progress() {
        // Cancelled RPCs release their capability references before objects are collected.
        for (auto& request : pending_) if (request.used) {
            if (os::Ipc::Handle(endpoint_, false).requestActive(request.token) != 1 || attempt(request)) {
                os::closeHandle(request.handle); request = {};
            }
        }
        for (size_t i = 0; i < capacity; ++i)
            if (pairs_[i].used && !live(pairs_[i], 0) && !live(pairs_[i], 1) &&
                !results_.references(pairs_[i].ends[0].token) && !results_.references(pairs_[i].ends[1].token)) release(i);
        if (os::Sys().readMonotonicTime() >= nextCollection_) {
            nextCollection_ = os::Sys().readMonotonicTime() + 1000000000ULL;
            for (uint32_t i = 0; i < services::ReplayCache::capacity; ++i) {
                const auto& record = results_.records[i]; if (!record.task) continue;
                uint64_t generation = 0; const auto status = checkpoint_.queryGeneration(record.task, generation);
                const bool dead = status == static_cast<int>(os::ErrorCode::NotFound) || (!status && generation != record.generation);
                if (dead && prepare()) {
                    results_.records[i] = {}; commit(capacity, i);
                }
            }
        }
    }
    void create(const os::IpcMessage& request, bool channel, uint32_t outcome) {
        for (size_t i = 0; i < capacity; ++i) if (!pairs_[i].used) {
            if (!prepare()) { reply(request.replyToken, -5); return; }
            auto& pair = pairs_[i]; pair.used = true; pair.channel = channel;
            for (auto& stream : pair.streams) { stream.head = stream.count = stream.eof = 0; }
            for (auto& end : pair.ends) {
                end.handle = os::detail::EndpointControl().create(); end.id = os::Ipc::Handle(end.handle, false).queryIdentity(); end.nonblocking = false;
            }
            if (!pair.ends[0].handle || !pair.ends[1].handle) {
                for (auto& end : pair.ends) closeTag(end.handle);
                pair = {}; reply(request.replyToken, -5); return;
            }
            os::IpcMessage result{}; result.handleCount = 2; result.length = 1;
            for (size_t n = 0; n < 2; ++n) {
                result.handles[n] = pair.ends[n].handle; result.handleOptions[n] = os::ipcHandleRestrictRights;
                result.handleRights[n] = channel ? 3 : n == 0 ? 1 : 2;
            }
            Pending transaction{}; transaction.pair = i; transaction.outcome = outcome; transaction.token = request.replyToken;
            finish(transaction, 0, &result);
            return;
        }
        reply(request.replyToken, -5);
    }
    void handle(os::IpcMessage& request) {
        const auto operation = static_cast<os::PipeOperation>(request.type);
        if (operation == os::PipeOperation::Acknowledge) {
            const auto slot = results_.acknowledge(request, checkpoint_);
            if (slot != services::ReplayCache::noRecord) {
                if (!durableResults_[slot] && !results_.records[slot].complete) { results_.records[slot] = {}; reply(request.replyToken, 0); return; }
                if (!prepare()) { reply(request.replyToken, -5); return; }
                results_.records[slot] = {}; commit(capacity, slot);
            }
            reply(request.replyToken, 0); return;
        }
        services::ReplayCache::Request replay;
        const int resumed = results_.begin(request, replay, checkpoint_);
        if (resumed == 1) {
            os::IpcMessage result{};
            if (!results_.restoreReply(replay.slot, checkpoint_, result)) failed();
            os::replyMessage(endpoint_, request.replyToken, result);
            for (uint32_t i = 0; i < result.handleCount; ++i) os::closeHandle(result.handles[i]);
            return;
        }
        if (resumed) { reply(request.replyToken, resumed); return; }
        if (request.length < 3) { reply(request.replyToken, -1); return; }
        if (operation == os::PipeOperation::Create || operation == os::PipeOperation::CreateChannel) { create(request, operation == os::PipeOperation::CreateChannel, replay.slot); return; }
        if (request.handleCount != 1) { reply(request.replyToken, -2); return; }
        const uint64_t identity = os::Ipc::Handle(request.handles[0], false).queryIdentity();
        for (size_t index = 0; index < capacity; ++index) if (pairs_[index].used) for (size_t side = 0; side < 2; ++side) {
            auto& pair = pairs_[index];
            if (pair.ends[side].id != identity) continue;
            if (operation == os::PipeOperation::Identity) { reply(request.replyToken, pair.channel ? identity : 0); return; }
            if (operation == os::PipeOperation::Nonblocking) {
                if (request.data[1] > 1) { reply(request.replyToken, -1); return; }
                if (!prepare()) { reply(request.replyToken, -5); return; }
                pair.ends[side].nonblocking = request.data[1];
                Pending transaction{}; transaction.pair = index; transaction.outcome = replay.slot; transaction.token = request.replyToken;
                finish(transaction, 0); return;
            }
            if (operation == os::PipeOperation::Eof) {
                if (!pair.channel || !(request.handleRights[0] & 2)) { reply(request.replyToken, -3); return; }
                if (pair.streams[1 - side].eof == UINT32_MAX || !prepare()) { reply(request.replyToken, -5); return; }
                ++pair.streams[1 - side].eof;
                Pending transaction{}; transaction.pair = index; transaction.outcome = replay.slot; transaction.token = request.replyToken;
                finish(transaction, 0);
                return;
            }
            if ((operation != os::PipeOperation::Read && operation != os::PipeOperation::Write) ||
                !request.data[1] || request.data[1] > 512) { reply(request.replyToken, -1); return; }
            const bool read = operation == os::PipeOperation::Read;
            if (!(request.handleRights[0] & (read ? 1 : 2)) || (!read && request.data[1] > (request.length - 3) * 8)) { reply(request.replyToken, -3); return; }
            Pending pending{}; pending.used = true; pending.pair = index; pending.end = side;
            pending.operation = operation; pending.size = request.data[1]; pending.token = request.replyToken;
            pending.outcome = replay.slot;
            if (!read) os::Runtime::copyBytes(pending.data, &request.data[3], pending.size);
            if (attempt(pending)) return;
            for (auto& slot : pending_) if (!slot.used) {
                pending.handle = request.handles[0]; slot = pending; request.handleCount = 0; return;
            }
            reply(request.replyToken, -5); return;
        }
        reply(request.replyToken, -2);
    }
 public:
    int run() {
        if (!restore()) return 1;

        if (!os::detail::RegistryClient(os::getBootstrapHandle()).registerName("pipe", endpoint_)) return 1;
        os::Sys().log("[PIPE] Ring 3 service ready\n");
        while (true) {
            progress();
            os::IpcMessage request{};
            if (os::receiveMessage(endpoint_, request, os::Sys().readMonotonicTime() + 10000000) == 0) {
                handle(request);
                for (uint32_t n = 0; n < request.handleCount; ++n) os::closeHandle(request.handles[n]);
                progress();
            }
        }
    }
};
int main(int, char**) {
    static PipeServer server;
    return server.run();
}
