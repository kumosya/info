/*
 * src/task/msgq/main.cc
 * 命名消息队列服务，管理容量、等待请求、消息收发与对象回收。
 */

#include "service/common/protocol.h"
#include <os/detail/client.h>
#include "service/common/records.h"
#include <os/runtime.h>
#include "service/task/msgq-records.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

class MessageQueueServer {
        static constexpr size_t queueCapacity = 16, handleCapacity = 64,
                                pendingCapacity = 128;

        struct Message {
                uint32_t size = 0, priority = 0;
                unsigned char data[256]{};
        };

        struct Queue {
                bool used = false, linked = false;
                char name[64]{};
                uint32_t maximum = 0, size = 0, count = 0;
                Message messages[16];
                uint64_t notificationTask = 0, notificationHandle = 0;
                uint32_t notificationSignal = 0;
        } queues_[queueCapacity];

        struct Open {
                uint64_t handle = 0, identity = 0;
                uint32_t queue   = 0;
                bool nonblocking = false;
        } opens_[handleCapacity];

        struct Pending {
                bool used = false, send = false;
                uint32_t opened = 0, size = 0, priority = 0;
                uint64_t token = 0, handle = 0;
                unsigned char data[256]{};
        } pending_[pendingCapacity];

        uint64_t endpoint_ = 0;

        /*
         * 构造消息队列操作回复并按调用令牌发送，可选择附带已经准备好的数据。
         */
        bool reply(uint64_t token, int64_t status,
                   os::IpcMessage* value = nullptr) {
            os::IpcMessage empty{};
            os::IpcMessage& result = value ? *value : empty;
            result.data[0]         = status;

            if (!result.length) {
                result.length = 1;
            }

            return os::replyMessage(endpoint_, token, result) == 0;
        }

        /*
         * 从请求中解析队列名称，确认消息边界内存在字符串终止符。
         */
        static const char* name(const os::IpcMessage& request) {
            if (request.length < 11) {
                return nullptr;
            }

            const char* value = reinterpret_cast<const char*>(&request.data[3]);

            if (value[0] != '/' || !value[1]) {
                return nullptr;
            }

            for (size_t i = 1; i < 64; ++i) {
                if (!value[i]) {
                    return value;
                }
            }

            return nullptr;
        }

        /*
         * 回收已经没有客户端使用且满足删除条件的队列及句柄，取消无法继续的等待请求。
         */
        void collect() {
            for (Queue& queue : queues_) {
                if (queue.used && queue.notificationTask) {
                    services::wire::TaskInfo owner{};

                    if (os::detail::TaskQuery().query(queue.notificationTask,
                                                      owner) ||
                        owner.state == 2) {
                        queue.notificationTask = queue.notificationHandle =
                            queue.notificationSignal = 0;
                    }
                }
            }

            // 引用只剩服务自己的端点表示客户端已经关闭，先撤销它注册的通知，再释放打开记录。
            for (Open& opened : opens_) {
                if (opened.handle &&
                    os::Ipc::Handle(opened.handle, false).queryReferences() <=
                        1) {
                    Queue& queue = queues_[opened.queue];

                    if (queue.notificationHandle == opened.identity) {
                        queue.notificationTask = queue.notificationHandle =
                            queue.notificationSignal = 0;
                    }

                    os::Ipc::Handle(opened.handle, false).destroyEndpoint();
                    os::closeHandle(opened.handle);
                    opened = {};
                }
            }

            for (size_t i = 0; i < queueCapacity; ++i) {
                if (queues_[i].used && !queues_[i].linked) {
                    bool live = false;

                    for (const Open& opened : opens_) {
                        if (opened.handle && opened.queue == i) {
                            live = true;
                        }
                    }

                    // 名称已删除且所有打开者都消失时才释放队列；仅删除名称不会影响现有描述符。
                    if (!live) {
                        queues_[i].used = false;
                    }
                }
            }
        }

        /*
         * 尝试完成一个排队的发送或接收。
         * 容量不足或没有消息时继续等待，成功后更新队列并回复调用者。
         */
        bool attempt(Pending& request) {
            const Open& opened = opens_[request.opened];
            Queue& queue       = queues_[opened.queue];

            // 发送与接收需要的条件相反：发送等空槽，接收等已有消息，非阻塞请求不能挂起。
            if (request.send) {
                if (queue.count == queue.maximum) {
                    if (!opened.nonblocking) {
                        return false;
                    }

                    reply(request.token, -6);

                    return true;
                }

                // 只有同步请求还接受回复时才把消息加入队列，取消了的发送不能留下幽灵消息。
                if (reply(request.token, 0)) {
                    const bool empty = queue.count == 0;
                    Message& message = queue.messages[queue.count++];
                    message.size     = request.size;
                    message.priority = request.priority;
                    os::Runtime::copyBytes(message.data, request.data,
                                           request.size);
                    bool waitingReceiver = false;

                    for (const Pending& pending : pending_) {
                        if (pending.used && !pending.send &&
                            opens_[pending.opened].queue == opened.queue &&
                            os::Ipc::Handle(endpoint_, false)
                                    .requestActive(pending.token) == 1) {
                            waitingReceiver = true;
                        }
                    }

                    // 从空变非空时可触发一次注册通知，但已有接收等待者时优先让接收者处理数据。
                    if (empty && queue.notificationHandle && !waitingReceiver) {
                        const uint64_t task    = queue.notificationTask;
                        const uint32_t signal  = queue.notificationSignal;
                        queue.notificationTask = queue.notificationHandle =
                            queue.notificationSignal = 0;

                        if (signal) {
                            os::detail::TaskControl().sendSignal(task, signal);
                        }
                    }
                }
            } else {
                if (!queue.count) {
                    if (!opened.nonblocking) {
                        return false;
                    }

                    reply(request.token, -6);

                    return true;
                }

                // 接收选择最高优先级消息；相同优先级保留先入先出顺序，不简单取数组末尾。
                uint32_t selected = 0;

                for (uint32_t i = 1; i < queue.count; ++i) {
                    if (queue.messages[i].priority >
                        queue.messages[selected].priority) {
                        selected = i;
                    }
                }

                const Message& message = queue.messages[selected];
                os::IpcMessage result{};
                result.data[1] = message.priority;
                result.length  = 2 + (message.size + 7) / 8;
                os::Runtime::copyBytes(&result.data[2], message.data,
                                       message.size);

                if (reply(request.token, message.size, &result)) {
                    for (uint32_t i = selected; i + 1 < queue.count; ++i) {
                        queue.messages[i] = queue.messages[i + 1];
                    }

                    --queue.count;
                }
            }

            return true;
        }

        /*
         * 扫描未完成请求，反复推进已经具备条件的操作，直到没有请求能继续。
         */

        // 发送完成会让接收具备条件，接收完成又会腾出发送空间，因此每轮要继续推进直到没有进展。
        void progress() {
            for (Pending& request : pending_) {
                if (request.used &&
                    (os::Ipc::Handle(endpoint_, false)
                             .requestActive(request.token) != 1 ||
                     attempt(request))) {
                    os::closeHandle(request.handle);
                    request = {};
                }
            }

            collect();
        }

        /*
         * 打开或创建命名消息队列。
         * 校验名称、创建标志和容量，登记访问句柄并处理排他创建。
         */
        void open(const os::IpcMessage& request) {
            const char* value    = name(request);
            const uint64_t flags = request.data[0];

            if (!value || (flags & 3) > 2) {
                reply(request.replyToken, -1);

                return;
            }

            // 先查仍有名称的现有队列，已经 unlink
            // 的旧对象即使还活着，也不能被新名字查找命中。
            size_t queueIndex = queueCapacity;

            for (size_t i = 0; i < queueCapacity; ++i) {
                if (queues_[i].used && queues_[i].linked &&
                    os::Runtime::stringsEqual(queues_[i].name, value)) {
                    queueIndex = i;
                }
            }

            bool created = false;

            // 排他创建遇到已有名称立即失败，不能把已有队列当成本次新建对象。
            if (queueIndex != queueCapacity && (flags & 0xc0) == 0xc0) {
                reply(request.replyToken, -8);

                return;
            }

            if (queueIndex == queueCapacity) {
                if (!(flags & 0x40)) {
                    reply(request.replyToken, -4);

                    return;
                }

                if (!request.data[1] || request.data[1] > 16 ||
                    !request.data[2] || request.data[2] > 256) {
                    reply(request.replyToken, -1);

                    return;
                }

                for (size_t i = 0; i < queueCapacity; ++i) {
                    if (!queues_[i].used) {
                        queueIndex = i;
                        break;
                    }
                }

                if (queueIndex == queueCapacity) {
                    reply(request.replyToken, -5);

                    return;
                }

                Queue& queue = queues_[queueIndex];
                queue        = {};
                queue.used = queue.linked = true;
                os::Runtime::copyString(queue.name, value, sizeof(queue.name));
                queue.maximum = request.data[1];
                queue.size    = request.data[2];
                created       = true;
            }

            // 队列对象和一次打开分别占记录；不同打开可以有不同非阻塞标志及访问权限。
            for (Open& opened : opens_) {
                if (!opened.handle) {
                    const uint64_t tag = os::detail::EndpointControl().create();

                    if (!tag) {
                        break;
                    }

                    opened = {tag, os::Ipc::Handle(tag, false).queryIdentity(),
                              static_cast<uint32_t>(queueIndex),
                              (flags & 0x800) != 0};
                    os::IpcMessage result{};
                    result.handleCount      = 1;
                    result.handles[0]       = tag;
                    result.handleOptions[0] = services::ipcHandleRestrictRights;
                    result.handleRights[0]  = (flags & 3) == 0   ? 1
                                              : (flags & 3) == 1 ? 2
                                                                 : 3;

                    // 若创建结果无法交付，归还本次打开的句柄，不能让没有客户端的打开记录长期存在。
                    if (!reply(request.replyToken, 0, &result)) {
                        os::Ipc::Handle(tag, false).destroyEndpoint();
                        os::closeHandle(tag);
                        opened = {};

                        if (created) {
                            queues_[queueIndex].used = false;
                        }
                    }

                    return;
                }
            }

            if (created) {
                queues_[queueIndex].used = false;
            }

            reply(request.replyToken, -5);
        }

        /*
         * 分发消息队列请求，包括打开、发送、接收、属性和删除；阻塞操作保存为等待记录，之后由
         * progress 推进。
         */
        void handle(os::IpcMessage& request) {
            if (request.length < 3) {
                reply(request.replyToken, -1);

                return;
            }

            const services::MessageQueueOperation operation =
                static_cast<services::MessageQueueOperation>(request.type);

            if (operation == services::MessageQueueOperation::Open) {
                open(request);

                return;
            }

            if (operation == services::MessageQueueOperation::Unlink) {
                const char* value = name(request);

                if (!value) {
                    reply(request.replyToken, -1);

                    return;
                }

                for (Queue& queue : queues_) {
                    if (queue.used && queue.linked &&
                        os::Runtime::stringsEqual(queue.name, value)) {
                        if (reply(request.replyToken, 0)) {
                            queue.linked = false;
                        }

                        return;
                    }
                }

                reply(request.replyToken, -4);

                return;
            }

            if (request.handleCount != 1) {
                reply(request.replyToken, -2);

                return;
            }

            // 先根据传入端点身份定位打开记录，不能把客户端提供的数组下标直接作为队列访问权。
            const uint64_t identity =
                os::Ipc::Handle(request.handles[0], false).queryIdentity();

            for (size_t index = 0; index < handleCapacity; ++index) {
                Open& opened = opens_[index];

                if (!opened.handle || opened.identity != identity) {
                    continue;
                }

                Queue& queue = queues_[opened.queue];

                // 属性查询只读取队列容量和当前数量，同时返回当前打开记录自己的非阻塞标志。
                if (operation == services::MessageQueueOperation::Attributes) {
                    const services::wire::MessageQueueAttributes attributes{
                        opened.nonblocking ? 0x800ULL : 0, queue.maximum,
                        queue.size, queue.count};
                    os::IpcMessage result{};
                    result.length = 5;
                    os::Runtime::copyBytes(&result.data[1], &attributes,
                                           sizeof(attributes));
                    reply(request.replyToken, 0, &result);

                    return;
                }

                // 修改标志只作用于本次打开记录，先成功回复才提交，取消了的控制请求不能产生额外变化。
                if (operation == services::MessageQueueOperation::Flags) {
                    if (request.data[1] & ~0x800ULL) {
                        reply(request.replyToken, -1);

                        return;
                    }

                    os::IpcMessage result{};
                    result.length  = 2;
                    result.data[1] = opened.nonblocking ? 0x800 : 0;

                    if (reply(request.replyToken, 0, &result)) {
                        opened.nonblocking = request.data[1] & 0x800;
                    }

                    return;
                }

                // 通知注册是一次性的，并与注册者身份绑定；另一个打开者不能随意覆盖已有通知。
                if (operation == services::MessageQueueOperation::Notify) {
                    if (request.data[1] > 2 ||
                        (request.data[1] == 2 &&
                         (!request.data[2] || request.data[2] > 64))) {
                        reply(request.replyToken, -1);

                        return;
                    }

                    if (!request.data[1]) {
                        if (queue.notificationHandle &&
                            queue.notificationTask != request.senderPid) {
                            reply(request.replyToken, -3);

                            return;
                        }

                        queue.notificationTask = queue.notificationHandle =
                            queue.notificationSignal = 0;
                        reply(request.replyToken, 0);

                        return;
                    }

                    if (queue.notificationHandle) {
                        reply(request.replyToken, -8);

                        return;
                    }

                    queue.notificationTask   = request.senderPid;
                    queue.notificationHandle = identity;
                    queue.notificationSignal =
                        request.data[1] == 2 ? request.data[2] : 0;
                    reply(request.replyToken, 0);

                    return;
                }

                const bool send = operation == services::MessageQueueOperation::Send;

                if ((!send &&
                     operation != services::MessageQueueOperation::Receive) ||
                    request.data[1] > 256 ||
                    (send && (request.data[1] > queue.size ||
                              request.data[1] > (request.length - 3) * 8 ||
                              request.data[2] > UINT32_MAX)) ||
                    (!send && request.data[1] < queue.size)) {
                    reply(request.replyToken, -1);

                    return;
                }

                if (!(request.handleRights[0] & (send ? 2 : 1))) {
                    reply(request.replyToken, -3);

                    return;
                }

                // 把消息数据和等待所需身份复制进独立记录，原 IPC
                // 请求缓冲区在本轮结束后即可复用。
                Pending pending{};
                pending.used     = true;
                pending.send     = send;
                pending.opened   = index;
                pending.size     = request.data[1];
                pending.priority = request.data[2];
                pending.token    = request.replyToken;

                if (send) {
                    os::Runtime::copyBytes(pending.data, &request.data[3],
                                           pending.size);
                }

                if (attempt(pending)) {
                    return;
                }

                // 无法立即完成的阻塞操作占用等待槽，后续消息进出时重新尝试；没有槽位则明确返回资源不足。
                for (Pending& slot : pending_) {
                    if (!slot.used) {
                        pending.handle      = request.handles[0];
                        slot                = pending;
                        request.handleCount = 0;

                        return;
                    }
                }

                reply(request.replyToken, -5);

                return;
            }

            reply(request.replyToken, -2);
        }

    public:
        /*
         * 注册消息队列服务端点并运行接收循环，处理请求、超时和资源回收。
         */
        int run() {
            endpoint_ = os::detail::EndpointControl().create();

            if (!endpoint_ ||
                !os::detail::RegistryClient(os::getBootstrapHandle())
                     .registerName("msgq", endpoint_)) {
                return 1;
            }

            os::Sys().log("[MSGQ] Ring 3 service ready\n");

            while (true) {
                progress();
                os::IpcMessage request{};

                if (os::receiveMessage(
                        endpoint_, request,
                        os::Sys().readMonotonicTime() + 10000000) == 0) {
                    handle(request);

                    for (uint32_t n = 0; n < request.handleCount; ++n) {
                        os::closeHandle(request.handles[n]);
                    }

                    progress();
                }
            }
        }
};

/*
 * 创建消息队列服务对象，进入服务主循环。
 */
int main(int, char**) {
    static MessageQueueServer server;

    return server.run();
}
