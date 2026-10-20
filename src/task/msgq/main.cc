#include "os/abi.h"
#include "os/core_service.h"
#include "os/service.h"
#include "os/task.h"

class MessageQueueServer {
    static constexpr size_t queueCapacity = 16, handleCapacity = 64, pendingCapacity = 128;
    struct Message { uint32_t size = 0, priority = 0; unsigned char data[256]{}; };
    struct Queue {
        bool used = false, linked = false; char name[64]{};
        uint32_t maximum = 0, size = 0, count = 0;
        Message messages[16];
        uint64_t notificationTask = 0, notificationHandle = 0; uint32_t notificationSignal = 0;
    } queues_[queueCapacity];
    struct Open { uint64_t handle = 0, identity = 0; uint32_t queue = 0; bool nonblocking = false; } opens_[handleCapacity];
    struct Pending {
        bool used = false, send = false; uint32_t opened = 0, size = 0, priority = 0;
        uint64_t token = 0, handle = 0; unsigned char data[256]{};
    } pending_[pendingCapacity];
    uint64_t endpoint_ = 0;
    bool reply(uint64_t token, int64_t status, os::IpcMessage* value = nullptr) {
        os::IpcMessage empty{}; auto& result = value ? *value : empty;
        result.data[0] = status; if (!result.length) result.length = 1;
        return os::replyMessage(endpoint_, token, result) == 0;
    }
    static const char* name(const os::IpcMessage& request) {
        if (request.length < 11) return nullptr;
        const auto* value = reinterpret_cast<const char*>(&request.data[3]);
        if (value[0] != '/' || !value[1]) return nullptr;
        for (size_t i = 1; i < 64; ++i) if (!value[i]) return value;
        return nullptr;
    }
    void collect() {
        for (auto& queue : queues_) if (queue.used && queue.notificationTask) {
            os::TaskInfo owner{};
            if (os::Task::query(queue.notificationTask, owner) || owner.state == 2)
                queue.notificationTask = queue.notificationHandle = queue.notificationSignal = 0;
        }
        for (auto& opened : opens_) if (opened.handle && os::Handle::references(opened.handle) <= 1) {
            auto& queue = queues_[opened.queue];
            if (queue.notificationHandle == opened.identity) queue.notificationTask = queue.notificationHandle = queue.notificationSignal = 0;
            os::Handle::destroyEndpoint(opened.handle); os::closeHandle(opened.handle); opened = {};
        }
        for (size_t i = 0; i < queueCapacity; ++i) if (queues_[i].used && !queues_[i].linked) {
            bool live = false; for (const auto& opened : opens_) if (opened.handle && opened.queue == i) live = true;
            if (!live) queues_[i].used = false;
        }
    }
    bool attempt(Pending& request) {
        const auto& opened = opens_[request.opened]; auto& queue = queues_[opened.queue];
        if (request.send) {
            if (queue.count == queue.maximum) {
                if (!opened.nonblocking) return false;
                reply(request.token, -6); return true;
            }
            if (reply(request.token, 0)) {
                const bool empty = queue.count == 0;
                auto& message = queue.messages[queue.count++]; message.size = request.size; message.priority = request.priority;
                os::Runtime::copyBytes(message.data, request.data, request.size);
                bool waitingReceiver = false;
                for (const auto& pending : pending_) if (pending.used && !pending.send &&
                    opens_[pending.opened].queue == opened.queue && os::Handle::requestActive(endpoint_, pending.token) == 1) waitingReceiver = true;
                if (empty && queue.notificationHandle && !waitingReceiver) {
                    const auto task = queue.notificationTask; const auto signal = queue.notificationSignal;
                    queue.notificationTask = queue.notificationHandle = queue.notificationSignal = 0;
                    if (signal) os::Task::sendSignal(task, signal);
                }
            }
        } else {
            if (!queue.count) {
                if (!opened.nonblocking) return false;
                reply(request.token, -6); return true;
            }
            uint32_t selected = 0;
            for (uint32_t i = 1; i < queue.count; ++i) if (queue.messages[i].priority > queue.messages[selected].priority) selected = i;
            const auto& message = queue.messages[selected];
            os::IpcMessage result{}; result.data[1] = message.priority; result.length = 2 + (message.size + 7) / 8;
            os::Runtime::copyBytes(&result.data[2], message.data, message.size);
            if (reply(request.token, message.size, &result)) {
                for (uint32_t i = selected; i + 1 < queue.count; ++i) queue.messages[i] = queue.messages[i + 1];
                --queue.count;
            }
        }
        return true;
    }
    void progress() {
        for (auto& request : pending_) if (request.used &&
            (os::Handle::requestActive(endpoint_, request.token) != 1 || attempt(request))) {
            os::closeHandle(request.handle); request = {};
        }
        collect();
    }
    void open(const os::IpcMessage& request) {
        const char* value = name(request); const uint64_t flags = request.data[0];
        if (!value || (flags & 3) > 2) { reply(request.replyToken, -1); return; }
        size_t queueIndex = queueCapacity;
        for (size_t i = 0; i < queueCapacity; ++i) if (queues_[i].used && queues_[i].linked && os::Runtime::stringsEqual(queues_[i].name, value)) queueIndex = i;
        bool created = false;
        if (queueIndex != queueCapacity && (flags & 0xc0) == 0xc0) { reply(request.replyToken, -8); return; }
        if (queueIndex == queueCapacity) {
            if (!(flags & 0x40)) { reply(request.replyToken, -4); return; }
            if (!request.data[1] || request.data[1] > 16 || !request.data[2] || request.data[2] > 256) { reply(request.replyToken, -1); return; }
            for (size_t i = 0; i < queueCapacity; ++i) if (!queues_[i].used) { queueIndex = i; break; }
            if (queueIndex == queueCapacity) { reply(request.replyToken, -5); return; }
            auto& queue = queues_[queueIndex]; queue = {}; queue.used = queue.linked = true;
            os::Runtime::copyString(queue.name, value, sizeof(queue.name)); queue.maximum = request.data[1]; queue.size = request.data[2]; created = true;
        }
        for (auto& opened : opens_) if (!opened.handle) {
            const uint64_t tag = os::Endpoint::create();
            if (!tag) break;
            opened = {tag, os::Handle::identity(tag), static_cast<uint32_t>(queueIndex), (flags & 0x800) != 0};
            os::IpcMessage result{}; result.handleCount = 1; result.handles[0] = tag;
            result.handleOptions[0] = os::ipcHandleRestrictRights; result.handleRights[0] = (flags & 3) == 0 ? 1 : (flags & 3) == 1 ? 2 : 3;
            if (!reply(request.replyToken, 0, &result)) {
                os::Handle::destroyEndpoint(tag); os::closeHandle(tag); opened = {};
                if (created) queues_[queueIndex].used = false;
            }
            return;
        }
        if (created) queues_[queueIndex].used = false;
        reply(request.replyToken, -5);
    }
    void handle(os::IpcMessage& request) {
        if (request.length < 3) { reply(request.replyToken, -1); return; }
        const auto operation = static_cast<os::MessageQueueOperation>(request.type);
        if (operation == os::MessageQueueOperation::Open) { open(request); return; }
        if (operation == os::MessageQueueOperation::Unlink) {
            const char* value = name(request); if (!value) { reply(request.replyToken, -1); return; }
            for (auto& queue : queues_) if (queue.used && queue.linked && os::Runtime::stringsEqual(queue.name, value)) {
                if (reply(request.replyToken, 0)) queue.linked = false;
                return;
            }
            reply(request.replyToken, -4); return;
        }
        if (request.handleCount != 1) { reply(request.replyToken, -2); return; }
        const uint64_t identity = os::Handle::identity(request.handles[0]);
        for (size_t index = 0; index < handleCapacity; ++index) {
            auto& opened = opens_[index]; if (!opened.handle || opened.identity != identity) continue;
            auto& queue = queues_[opened.queue];
            if (operation == os::MessageQueueOperation::Attributes) {
                const os::MessageQueueAttributes attributes{opened.nonblocking ? 0x800ULL : 0, queue.maximum, queue.size, queue.count};
                os::IpcMessage result{}; result.length = 5; os::Runtime::copyBytes(&result.data[1], &attributes, sizeof(attributes)); reply(request.replyToken, 0, &result); return;
            }
            if (operation == os::MessageQueueOperation::Flags) {
                if (request.data[1] & ~0x800ULL) { reply(request.replyToken, -1); return; }
                os::IpcMessage result{}; result.length = 2; result.data[1] = opened.nonblocking ? 0x800 : 0;
                if (reply(request.replyToken, 0, &result)) opened.nonblocking = request.data[1] & 0x800;
                return;
            }
            if (operation == os::MessageQueueOperation::Notify) {
                if (request.data[1] > 2 || (request.data[1] == 2 && (!request.data[2] || request.data[2] > 64))) { reply(request.replyToken, -1); return; }
                if (!request.data[1]) {
                    if (queue.notificationHandle && queue.notificationTask != request.senderPid) { reply(request.replyToken, -3); return; }
                    queue.notificationTask = queue.notificationHandle = queue.notificationSignal = 0; reply(request.replyToken, 0); return;
                }
                if (queue.notificationHandle) { reply(request.replyToken, -8); return; }
                queue.notificationTask = request.senderPid; queue.notificationHandle = identity;
                queue.notificationSignal = request.data[1] == 2 ? request.data[2] : 0; reply(request.replyToken, 0); return;
            }
            const bool send = operation == os::MessageQueueOperation::Send;
            if ((!send && operation != os::MessageQueueOperation::Receive) || request.data[1] > 256 ||
                (send && (request.data[1] > queue.size || request.data[1] > (request.length - 3) * 8 || request.data[2] > UINT32_MAX)) ||
                (!send && request.data[1] < queue.size)) { reply(request.replyToken, -1); return; }
            if (!(request.handleRights[0] & (send ? 2 : 1))) { reply(request.replyToken, -3); return; }
            Pending pending{}; pending.used = true; pending.send = send; pending.opened = index; pending.size = request.data[1]; pending.priority = request.data[2]; pending.token = request.replyToken;
            if (send) os::Runtime::copyBytes(pending.data, &request.data[3], pending.size);
            if (attempt(pending)) return;
            for (auto& slot : pending_) if (!slot.used) { pending.handle = request.handles[0]; slot = pending; request.handleCount = 0; return; }
            reply(request.replyToken, -5); return;
        }
        reply(request.replyToken, -2);
    }
 public:
    int run() {
        endpoint_ = os::Endpoint::create();
        if (!endpoint_ || !os::ServiceRegistry::registerService(os::getBootstrapHandle(), "msgq", endpoint_)) return 1;
        os::System::log("[MSGQ] Ring 3 service ready\n");
        for (;;) {
            progress(); os::IpcMessage request{};
            if (os::receiveMessage(endpoint_, request, os::Clock::getMonotonicTime() + 10000000) == 0) {
                handle(request); for (uint32_t n = 0; n < request.handleCount; ++n) os::closeHandle(request.handles[n]); progress();
            }
        }
    }
};
int main(int, char**) { static MessageQueueServer server; return server.run(); }
