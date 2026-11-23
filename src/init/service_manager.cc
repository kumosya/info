/* 服务对象的成员函数实现。 */
#include "service/init/init.h"

namespace init {

    /* 建立服务注册中心的管理端点。 */
    bool InitServiceManager::init() {
        endpoint_ = os::detail::EndpointControl().create();
        if (!endpoint_) {
            return false;
        }

        os::detail::RegistryClient().setDefault(endpoint_);
        constexpr uint64_t stack = 0x70000000;
        constexpr uint64_t size = 16 * 4096;
        return os::mapMemory(stack, size, 1) == stack &&
               os::createThread(threadMain, stack + size, this) != 0;
    }

    /* 检查服务注册名称。 */
    bool InitServiceManager::validName(const os::IpcMessage& request) {
        if (request.length < 3) {
            return false;
        }

        const char* name = reinterpret_cast<const char*>(request.data);
        for (uint32_t i = 0; i < services::serviceNameCapacity; ++i) {
            const char value = name[i];
            if (!value) {
                return i != 0;
            }

            if (!((value >= 'a' && value <= 'z') ||
                  (value >= '0' && value <= '9') || value == '-' ||
                  value == '_' || value == '.')) {
                return false;
            }
        }

        return false;
    }

    /* 查询核心服务应具有的角色。 */
    services::ServiceRole InitServiceManager::requiredRole(const char* name) {
        /* 记录不允许普通服务覆盖的受保护服务名称和身份。 */
        struct Protected {
                const char* name;
                services::ServiceRole role;
        };

        const Protected protectedNames[] = {
            {"services", services::ServiceRole::Init},
            {"pipe", services::ServiceRole::Pipe},
            {"control.init", services::ServiceRole::Init},
            {"control.task", services::ServiceRole::Task},
            {"control.device", services::ServiceRole::Device},
            {"msgq", services::ServiceRole::MessageQueue},
            {"memory.kernel", services::ServiceRole::Init},
            {"task.kernel", services::ServiceRole::Init},
            {"system.kernel", services::ServiceRole::Init},
            {"device.kernel", services::ServiceRole::Init},
            {"pci.kernel", services::ServiceRole::Init},
            {"pci", services::ServiceRole::Pci},
            {"task", services::ServiceRole::Task},
            {"fs", services::ServiceRole::Fs},
            {"device", services::ServiceRole::Device},
            {"input", services::ServiceRole::Device},
            {"storage", services::ServiceRole::Storage},
            {"tty", services::ServiceRole::Tty},
            {"console", services::ServiceRole::Console},
            {"terminal", services::ServiceRole::Console},
            {"guiw", services::ServiceRole::Window},
            {"users", services::ServiceRole::Users},
            {"mm", services::ServiceRole::Memory}};
        for (const Protected& item : protectedNames) {
            if (os::Runtime::stringsEqual(name, item.name)) {
                return item.role;
            }
        }

        return services::ServiceRole::None;
    }

    /* 撤销已经失效的注册记录。 */
    void InitServiceManager::invalidate(Record& record) {
        if (record.endpoint) {
            os::closeHandle(record.endpoint);
        }

        record.endpoint = 0;
        record.publisher = 0;
        ++record.generation;
    }

    /* 检查注册服务的存活状态。 */
    void InitServiceManager::sweep() {
        for (uint32_t i = 0; i < count_; ++i) {
            Record& record = records_[i];
            if (!record.endpoint) {
                continue;
            }

            services::wire::TaskInfo info{};
            if (os::Ipc::Handle(record.endpoint, false).queryState() != 0 ||
                !query(record.publisher, info) ||
                info.serviceRole != static_cast<uint32_t>(record.role)) {
                invalidate(record);
            }
        }
    }

    /* 处理服务注册和查找消息。 */
    void InitServiceManager::handle(os::IpcMessage& request,
                                    os::IpcMessage& reply) {
        reply.length = 2;
        reply.data[0] = static_cast<int64_t>(os::ErrorCode::InvalidArgument);
        if (!validName(request)) {
            return;
        }

        const char* name = reinterpret_cast<const char*>(request.data);
        services::wire::TaskInfo caller{};
        if (!query(request.senderPid, caller)) {
            reply.data[0] = static_cast<int64_t>(os::ErrorCode::PeerDied);
            return;
        }

        const services::RegistryRequest operation =
            static_cast<services::RegistryRequest>(request.type);
        Record* record = nullptr;
        for (uint32_t i = 0; i < count_; ++i) {
            if (os::Runtime::stringsEqual(records_[i].name, name)) {
                record = &records_[i];
                break;
            }
        }

        if (record && record->endpoint) {
            services::wire::TaskInfo publisher{};
            if (os::Ipc::Handle(record->endpoint, false).queryState() != 0 ||
                !query(record->publisher, publisher)) {
                invalidate(*record);
            }
        }

        if (operation == services::RegistryRequest::Register) {
            const services::ServiceRole required = requiredRole(name);
            if (request.length < 4 || request.handleCount != 1 ||
                (request.data[3] & ~services::serviceAccessOwnerOnly)) {
                return;
            }

            if ((required != services::ServiceRole::None &&
                 caller.serviceRole != static_cast<uint32_t>(required)) ||
                (record && record->endpoint &&
                 record->publisher != request.senderPid) ||
                (record && required == services::ServiceRole::None &&
                 record->uid != caller.effectiveUid)) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::PermissionDenied);
                return;
            }

            if (!record && count_ == capacity) {
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::Busy);
                return;
            }

            os::Ipc::Handle clientOwner;
            const int clientStatus = os::Ipc::Handle(request.handles[0], false)
                                         .duplicate(2, 0, clientOwner);
            const int64_t client =
                clientStatus ? clientStatus
                             : static_cast<int64_t>(clientOwner.release());
            if (client <= 0) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::InvalidHandle);
                return;
            }

            if (!record) {
                record = &records_[count_++];
                os::Runtime::copyString(record->name, name,
                                        sizeof(record->name));
            } else if (record->endpoint) {
                os::closeHandle(record->endpoint);
            }

            record->endpoint = client;
            record->publisher = request.senderPid;
            record->uid = caller.effectiveUid;
            record->role =
                static_cast<services::ServiceRole>(caller.serviceRole);
            record->access = request.data[3];
            ++record->generation;
            reply.data[0] = 0;
            reply.data[1] = record->generation;
        } else if (operation == services::RegistryRequest::Lookup) {
            if (!record || !record->endpoint) {
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::NotFound);
                return;
            }

            if ((record->access & services::serviceAccessOwnerOnly) &&
                record->uid != caller.effectiveUid) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::PermissionDenied);
                return;
            }

            reply.data[0] = 0;
            reply.data[1] = record->generation;
            reply.handleCount = 1;
            reply.data[2] = record->publisher;
            reply.length = 3;
            reply.handles[0] = record->endpoint;
            reply.handleOptions[0] = services::ipcHandleRestrictRights;
            reply.handleRights[0] = 2;
        } else if (operation == services::RegistryRequest::Unregister) {
            if (request.length < 5) {
                return;
            }

            if (!record || record->publisher != request.senderPid ||
                request.data[4] != record->generation) {
                reply.data[0] =
                    static_cast<int64_t>(os::ErrorCode::PermissionDenied);
                return;
            }

            invalidate(*record);
            reply.data[0] = 0;
            reply.data[1] = record->generation;
        }
    }

    /* 接收并分发服务注册请求。 */
    void InitServiceManager::run() {
        while (true) {
            sweep();
            os::IpcMessage request{}, reply{};
            const uint64_t deadline = os::Sys().readMonotonicTime() + 50000000;
            if (os::receiveMessage(endpoint_, request, deadline) != 0) {
                continue;
            }

            handle(request, reply);
            for (uint32_t i = 0; i < request.handleCount; ++i) {
                os::closeHandle(request.handles[i]);
            }

            os::replyMessage(endpoint_, request.replyToken, reply);
        }
    }

}  // namespace init
