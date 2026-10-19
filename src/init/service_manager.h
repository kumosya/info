#include "os/task.h"
#pragma once

#include "os/abi.h"
#include "os/core_service.h"
#include "os/protocol.h"
#include "os/service.h"

class ServiceManager {
   public:
    bool initialize() {
        endpoint_ = os::Endpoint::create();
        if (!endpoint_) return false;
        os::ServiceRegistry::setServiceRegistry(endpoint_);
        constexpr uint64_t stack = 0x70000000;
        constexpr uint64_t size = 16 * 4096;
        return os::mapMemory(stack, size, 1) == stack &&
            os::createThread(threadMain, stack + size, this) != 0;
    }
    uint64_t endpoint() const { return endpoint_; }

   private:
    struct Record {
        char name[os::serviceNameCapacity]{};
        uint64_t endpoint = 0;
        uint64_t publisher = 0;
        uint64_t generation = 0;
        uint32_t uid = 0;
        os::ServiceRole role = os::ServiceRole::None;
        uint64_t access = 0;
    };
    static constexpr uint32_t capacity = 64;
    Record records_[capacity]{};
    uint32_t count_ = 0;
    uint64_t endpoint_ = 0;

    static void threadMain(void* manager) { static_cast<ServiceManager*>(manager)->run(); }
    static bool query(uint64_t id, os::TaskInfo& info) {
        return os::Process::queryTask(id, info) == 0 && info.state != 2;
    }
    static bool validName(const os::IpcMessage& request) {
        if (request.length < 3) return false;
        const auto* name = reinterpret_cast<const char*>(request.data);
        for (uint32_t i = 0; i < os::serviceNameCapacity; ++i) {
            const char value = name[i];
            if (!value) return i != 0;
            if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
                value == '-' || value == '_' || value == '.')) return false;
        }
        return false;
    }
    static os::ServiceRole requiredRole(const char* name) {
        struct Protected { const char* name; os::ServiceRole role; };
        const Protected protectedNames[] = {
            {"services", os::ServiceRole::Init}, {"pipe", os::ServiceRole::Pipe},
            {"msgq", os::ServiceRole::MessageQueue}, {"memory.kernel", os::ServiceRole::Init},
            {"task.kernel", os::ServiceRole::Init}, {"system.kernel", os::ServiceRole::Init}, {"device.kernel", os::ServiceRole::Init}, {"pci.kernel", os::ServiceRole::Init},
            {"pci", os::ServiceRole::Pci}, {"task", os::ServiceRole::Task},
            {"fs", os::ServiceRole::Fs}, {"device", os::ServiceRole::Device},
            {"input", os::ServiceRole::Device}, {"storage", os::ServiceRole::Storage},
            {"tty", os::ServiceRole::Tty}, {"console", os::ServiceRole::Console},
            {"terminal", os::ServiceRole::Console}, {"guiw", os::ServiceRole::Window},
            {"users", os::ServiceRole::Users}, {"mm", os::ServiceRole::Memory}
        };
        for (const auto& item : protectedNames)
            if (os::Runtime::stringsEqual(name, item.name)) return item.role;
        return os::ServiceRole::None;
    }
    void invalidate(Record& record) {
        if (record.endpoint) os::closeHandle(record.endpoint);
        record.endpoint = 0;
        record.publisher = 0;
        ++record.generation;
    }
    void sweep() {
        for (uint32_t i = 0; i < count_; ++i) {
            auto& record = records_[i];
            if (!record.endpoint) continue;
            os::TaskInfo info{};
            if (os::Handle::state(record.endpoint) != 0 || !query(record.publisher, info) ||
                info.serviceRole != static_cast<uint32_t>(record.role)) invalidate(record);
        }
    }
    void handle(os::IpcMessage& request, os::IpcMessage& reply) {
        reply.length = 2;
        reply.data[0] = static_cast<int64_t>(os::ErrorCode::InvalidArgument);
        if (!validName(request)) return;
        const auto* name = reinterpret_cast<const char*>(request.data);
        os::TaskInfo caller{};
        if (!query(request.senderPid, caller)) { reply.data[0] = static_cast<int64_t>(os::ErrorCode::PeerDied); return; }
        const auto operation = static_cast<os::RegistryRequest>(request.type);
        Record* record = nullptr;
        for (uint32_t i = 0; i < count_; ++i)
            if (os::Runtime::stringsEqual(records_[i].name, name)) { record = &records_[i]; break; }
        if (record && record->endpoint) {
            os::TaskInfo publisher{};
            if (os::Handle::state(record->endpoint) != 0 || !query(record->publisher, publisher)) invalidate(*record);
        }
        if (operation == os::RegistryRequest::Register) {
            const auto required = requiredRole(name);
            if (request.length < 4 || request.handleCount != 1 || (request.data[3] & ~os::serviceAccessOwnerOnly)) return;
            if ((required != os::ServiceRole::None && caller.serviceRole != static_cast<uint32_t>(required)) ||
                (record && record->endpoint && record->publisher != request.senderPid) ||
                (record && required == os::ServiceRole::None && record->uid != caller.effectiveUid)) {
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::PermissionDenied); return;
            }
            if (!record && count_ == capacity) { reply.data[0] = static_cast<int64_t>(os::ErrorCode::Busy); return; }
            const int64_t client = os::Handle::duplicate(request.handles[0], 2);
            if (client <= 0) { reply.data[0] = static_cast<int64_t>(os::ErrorCode::InvalidHandle); return; }
            if (!record) {
                record = &records_[count_++];
                os::Runtime::copyString(record->name, name, sizeof(record->name));
            } else if (record->endpoint) os::closeHandle(record->endpoint);
            record->endpoint = client;
            record->publisher = request.senderPid;
            record->uid = caller.effectiveUid;
            record->role = static_cast<os::ServiceRole>(caller.serviceRole);
            record->access = request.data[3];
            ++record->generation;
            reply.data[0] = 0;
            reply.data[1] = record->generation;
        } else if (operation == os::RegistryRequest::Lookup) {
            if (!record || !record->endpoint) { reply.data[0] = static_cast<int64_t>(os::ErrorCode::NotFound); return; }
            if ((record->access & os::serviceAccessOwnerOnly) && record->uid != caller.effectiveUid) {
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::PermissionDenied); return;
            }
            reply.data[0] = 0;
            reply.data[1] = record->generation;
            reply.handleCount = 1;
            reply.handles[0] = record->endpoint;
            reply.handleOptions[0] = os::ipcHandleRestrictRights;
            reply.handleRights[0] = 2;
        } else if (operation == os::RegistryRequest::Unregister) {
            if (request.length < 5) return;
            if (!record || record->publisher != request.senderPid || request.data[4] != record->generation) {
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::PermissionDenied); return;
            }
            invalidate(*record);
            reply.data[0] = 0;
            reply.data[1] = record->generation;
        }
    }
    void run() {
        while (true) {
            sweep();
            os::IpcMessage request{}, reply{};
            const uint64_t deadline = os::Clock::getMonotonicTime() + 50000000;
            if (os::receiveMessage(endpoint_, request, deadline) != 0) continue;
            handle(request, reply);
            for (uint32_t i = 0; i < request.handleCount; ++i) os::closeHandle(request.handles[i]);
            os::replyMessage(endpoint_, request.replyToken, reply);
        }
    }
};
