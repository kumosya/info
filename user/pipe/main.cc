#include "os/abi.h"
#include "os/core_service.h"
#include "os/service.h"

class PipeServer {
    static constexpr size_t capacity = 64, pendingCapacity = 128, bufferSize = 16384;
    struct Stream { unsigned char bytes[bufferSize]{}; uint32_t head = 0, count = 0, eof = 0; };
    struct End { uint64_t handle = 0, id = 0; bool nonblocking = false; };
    struct Pair { bool used = false, channel = false; End ends[2]; Stream streams[2]; } pairs_[capacity];
    struct Pending {
        bool used = false; uint32_t pair = 0, end = 0, size = 0;
        os::PipeOperation operation{}; uint64_t token = 0, handle = 0;
        unsigned char data[512]{};
    } pending_[pendingCapacity];
    uint64_t endpoint_ = 0;

    static void closeTag(uint64_t handle) { if (handle) { os::Handle::destroyEndpoint(handle); os::closeHandle(handle); } }
    void release(size_t index) {
        auto& pair = pairs_[index];
        for (auto& end : pair.ends) closeTag(end.handle);
        pair.used = false;
    }
    bool live(const Pair& pair, size_t end) { return os::Handle::references(pair.ends[end].handle) > 1; }
    bool reply(uint64_t token, int64_t status, os::IpcMessage* value = nullptr) {
        os::IpcMessage empty{}; auto& result = value ? *value : empty;
        result.data[0] = status; if (!result.length) result.length = 1;
        return os::replyMessage(endpoint_, token, result) == 0;
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
            os::IpcMessage result{}; result.length = 1 + (count + 7) / 8;
            auto* data = reinterpret_cast<unsigned char*>(&result.data[1]);
            for (uint32_t i = 0; i < count; ++i) data[i] = stream.bytes[(stream.head + i) % bufferSize];
            if (reply(request.token, count, &result)) {
                stream.head = (stream.head + count) % bufferSize; stream.count -= count;
                if (!count && stream.eof) --stream.eof;
            }
            return true;
        }
        auto& stream = pair.streams[pair.channel ? 1 - end : 0];
        if (!live(pair, 1 - end)) { reply(request.token, -13); return true; }
        if (bufferSize - stream.count < request.size) {
            if (!self.nonblocking) return false;
            reply(request.token, -6); return true;
        }
        if (reply(request.token, request.size)) {
            for (uint32_t i = 0; i < request.size; ++i) stream.bytes[(stream.head + stream.count + i) % bufferSize] = request.data[i];
            stream.count += request.size;
        }
        return true;
    }
    void progress() {
        // Cancelled RPCs release their capability references before objects are collected.
        for (auto& request : pending_) if (request.used) {
            if (os::Handle::requestActive(endpoint_, request.token) != 1 || attempt(request)) {
                os::closeHandle(request.handle); request = {};
            }
        }
        for (size_t i = 0; i < capacity; ++i)
            if (pairs_[i].used && !live(pairs_[i], 0) && !live(pairs_[i], 1)) release(i);
    }
    void create(const os::IpcMessage& request, bool channel) {
        for (size_t i = 0; i < capacity; ++i) if (!pairs_[i].used) {
            auto& pair = pairs_[i]; pair.used = true; pair.channel = channel;
            for (auto& stream : pair.streams) { stream.head = stream.count = stream.eof = 0; }
            for (auto& end : pair.ends) {
                end.handle = os::Endpoint::create(); end.id = os::Handle::identity(end.handle); end.nonblocking = false;
            }
            if (!pair.ends[0].handle || !pair.ends[1].handle) { release(i); reply(request.replyToken, -5); return; }
            os::IpcMessage result{}; result.handleCount = 2; result.length = 1;
            for (size_t n = 0; n < 2; ++n) {
                result.handles[n] = pair.ends[n].handle; result.handleOptions[n] = os::ipcHandleRestrictRights;
                result.handleRights[n] = channel ? 3 : n == 0 ? 1 : 2;
            }
            if (!reply(request.replyToken, 0, &result)) release(i);
            return;
        }
        reply(request.replyToken, -5);
    }
    void handle(os::IpcMessage& request) {
        const auto operation = static_cast<os::PipeOperation>(request.type);
        if (request.length < 3) { reply(request.replyToken, -1); return; }
        if (operation == os::PipeOperation::Create || operation == os::PipeOperation::CreateChannel) { create(request, operation == os::PipeOperation::CreateChannel); return; }
        if (request.handleCount != 1) { reply(request.replyToken, -2); return; }
        const uint64_t identity = os::Handle::identity(request.handles[0]);
        for (size_t index = 0; index < capacity; ++index) if (pairs_[index].used) for (size_t side = 0; side < 2; ++side) {
            auto& pair = pairs_[index];
            if (pair.ends[side].id != identity) continue;
            if (operation == os::PipeOperation::Identity) { reply(request.replyToken, pair.channel ? identity : 0); return; }
            if (operation == os::PipeOperation::Nonblocking) {
                if (request.data[1] > 1) { reply(request.replyToken, -1); return; }
                pair.ends[side].nonblocking = request.data[1]; reply(request.replyToken, 0); return;
            }
            if (operation == os::PipeOperation::Eof) {
                if (!pair.channel || !(request.handleRights[0] & 2)) { reply(request.replyToken, -3); return; }
                if (reply(request.replyToken, 0)) ++pair.streams[1 - side].eof;
                return;
            }
            if ((operation != os::PipeOperation::Read && operation != os::PipeOperation::Write) ||
                !request.data[1] || request.data[1] > 512) { reply(request.replyToken, -1); return; }
            const bool read = operation == os::PipeOperation::Read;
            if (!(request.handleRights[0] & (read ? 1 : 2)) || (!read && request.data[1] > (request.length - 3) * 8)) { reply(request.replyToken, -3); return; }
            Pending pending{}; pending.used = true; pending.pair = index; pending.end = side;
            pending.operation = operation; pending.size = request.data[1]; pending.token = request.replyToken;
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
        endpoint_ = os::Endpoint::create();
        if (!endpoint_ || !os::ServiceRegistry::registerService(os::getBootstrapHandle(), "pipe", endpoint_)) return 1;
        os::System::log("[PIPE] Ring 3 service ready\n");
        for (;;) {
            progress();
            os::IpcMessage request{};
            if (os::receiveMessage(endpoint_, request, os::Clock::getMonotonicTime() + 10000000) == 0) {
                handle(request);
                for (uint32_t n = 0; n < request.handleCount; ++n) os::closeHandle(request.handles[n]);
                progress();
            }
        }
    }
};
int main(int, char**) { static PipeServer server; return server.run(); }
