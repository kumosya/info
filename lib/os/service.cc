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
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace os {

namespace {
uint64_t defaultRegistry;
}
Sys::Registry::Registry(uint64_t endpoint)
    : registry_(endpoint ? endpoint
                         : (defaultRegistry ? defaultRegistry
                                            : getBootstrapHandle())) {}
bool Sys::Registry::putName(os::IpcMessage& message, const char* name) {
    if (!name || !name[0] || Runtime::stringLength(name) >= serviceNameCapacity)
        return false;
    char* target = reinterpret_cast<char*>(message.data);
    os::Runtime::copyString(target, name, serviceNameCapacity);
    message.length = 5;
    return true;
}

void Sys::Registry::setDefault(uint64_t endpoint) {
    defaultRegistry = endpoint;
    registry_       = endpoint;
}

uint64_t Sys::Registry::getEndpoint() const { return registry_; }

bool Sys::Registry::registerRaw(const char* name, uint64_t endpoint,
                                uint64_t access, uint64_t* generation) {
    IpcMessage request{};
    request.type             = static_cast<uint64_t>(RegistryRequest::Register);
    request.handleCount      = 1;
    request.handles[0]       = endpoint;
    request.handleOptions[0] = ipcHandleRestrictRights;
    request.handleRights[0]  = 2;
    if (!Sys::Registry::putName(request, name)) return false;
    request.data[3] = access;
    IpcMessage reply{};
    const bool registered =
        callService(registry_, request, reply) == 0 && reply.data[0] == 0;
    if (registered && generation) *generation = reply.data[1];
    return registered;
}

uint64_t Sys::Registry::lookupRaw(const char* name, uint32_t attempts,
                                  uint64_t* generation) {
    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        IpcMessage request{};
        request.type = static_cast<uint64_t>(RegistryRequest::Lookup);
        if (!Sys::Registry::putName(request, name)) return 0;
        IpcMessage reply{};
        if (callService(registry_, request, reply) == 0 && reply.data[0] == 0 &&
            reply.handleCount == 1) {
            if (generation) *generation = reply.data[1];
            return reply.handles[0];
        }
        sleep(1);
    }
    return 0;
}

bool Sys::Registry::unregisterRaw(const char* name, uint64_t generation) {
    IpcMessage request{}, reply{};
    if (!putName(request, name)) return false;
    request.type    = static_cast<uint64_t>(RegistryRequest::Unregister);
    request.data[4] = generation;
    return callService(registry_, request, reply) == 0 && reply.data[0] == 0;
}

namespace {
uint64_t kernelHandles[static_cast<uint64_t>(CoreServiceKind::Count)];
}
uint64_t Sys::Transport::openHandle() {
    if (kind_ == CoreServiceKind::Pipe ||
        kind_ == CoreServiceKind::MessageQueue)
        return detail::RegistryClient().lookup(
            kind_ == CoreServiceKind::Pipe ? "pipe" : "msgq", 1000);
    if (kind_ == CoreServiceKind::Ipc) return Runtime::kernelBootstrapHandle();
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(IpcOperation::GetCoreService);
    request.length  = 5;
    request.data[0] = static_cast<uint64_t>(kind_);
    const int status =
        callService(Runtime::kernelBootstrapHandle(), request, reply);
    return status ? 0 : reply.data[0];
}

int Sys::Transport::callOnce(IpcMessage& request, IpcMessage& reply,
                             uint64_t deadline) {
    const auto kind = kind_;
    if (kind == CoreServiceKind::Ipc)
        return Runtime::kernelBootstrapHandle()
                   ? callService(Runtime::kernelBootstrapHandle(), request,
                                 reply, deadline)
                   : static_cast<int>(ErrorCode::InvalidHandle);
    const auto index = static_cast<uint64_t>(kind);
    if (index >= static_cast<uint64_t>(CoreServiceKind::Count))
        return static_cast<int>(ErrorCode::InvalidArgument);
    uint64_t handle = __atomic_load_n(&kernelHandles[index], __ATOMIC_ACQUIRE);
    if (!handle) {
        const int64_t created = openHandle();
        if (created <= 0) return static_cast<int>(ErrorCode::NotFound);
        if (Ipc::Handle(created, false).setFlags(handleFlagCloseOnExec) != 0) {
            closeHandle(created);
            return static_cast<int>(ErrorCode::InvalidHandle);
        }
        uint64_t expected = 0;
        if (__atomic_compare_exchange_n(&kernelHandles[index], &expected,
                                        created, false, __ATOMIC_RELEASE,
                                        __ATOMIC_ACQUIRE))
            handle = created;
        else {
            closeHandle(created);
            handle = expected;
        }
    }
    IpcMessage outgoing = request;
    const bool tagged =
        kind == CoreServiceKind::Pipe
            ? request.type != static_cast<uint64_t>(PipeOperation::Create) &&
                  request.type !=
                      static_cast<uint64_t>(PipeOperation::CreateChannel) &&
                  request.type !=
                      static_cast<uint64_t>(PipeOperation::Acknowledge)
            : kind == CoreServiceKind::MessageQueue &&
                  request.type !=
                      static_cast<uint64_t>(MessageQueueOperation::Open) &&
                  request.type !=
                      static_cast<uint64_t>(MessageQueueOperation::Unlink);
    if (tagged) {
        if (outgoing.handleCount)
            return static_cast<int>(ErrorCode::InvalidArgument);
        outgoing.handleCount = 1;
        outgoing.handles[0]  = request.data[0];
        outgoing.data[0]     = 0;
    }
    const int status = callService(handle, outgoing, reply, deadline);
    if (status == static_cast<int>(ErrorCode::PeerDied) ||
        status == static_cast<int>(ErrorCode::EndpointClosed) ||
        status == static_cast<int>(ErrorCode::InvalidHandle)) {
        uint64_t expected = handle;
        if (__atomic_compare_exchange_n(&kernelHandles[index], &expected, 0ULL,
                                        false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE))
            closeHandle(handle);
    }
    return status;
}

int Sys::Transport::call(IpcMessage& request, IpcMessage& reply,
                         uint64_t deadline) {
    const auto kind = kind_;
    if (kind != CoreServiceKind::Pipe ||
        request.type == static_cast<uint64_t>(PipeOperation::Acknowledge))
        return callOnce(request, reply, deadline);
    if (request.length > ipcDataWordCount - 3)
        return static_cast<int>(ErrorCode::InvalidArgument);
    static uint64_t sequence;
    struct PendingAcknowledgement {
        uint64_t id, thread;
        uint32_t state;
    };
    static PendingAcknowledgement pendingAcknowledgements[128];
    const auto thread = detail::TaskControl().getThreadId();
    if (static_cast<int64_t>(thread) <= 0)
        return static_cast<int>(ErrorCode::InvalidArgument);
    auto acknowledge = [&](uint64_t value, uint64_t origin) {
        IpcMessage message{}, result{};
        message.type    = static_cast<uint64_t>(PipeOperation::Acknowledge);
        message.length  = 2;
        message.data[0] = value;
        message.data[1] = origin;
        return !callOnce(message, result,
                         Sys().readMonotonicTime() + 1000000000ULL) &&
               !static_cast<int64_t>(result.data[0]);
    };
    auto defer = [&](uint64_t value, uint64_t origin) {
        for (auto& slot : pendingAcknowledgements) {
            uint32_t empty = 0;
            if (!__atomic_compare_exchange_n(&slot.state, &empty, 1U, false,
                                             __ATOMIC_ACQUIRE,
                                             __ATOMIC_RELAXED))
                continue;
            slot.id     = value;
            slot.thread = origin;
            __atomic_store_n(&slot.state, 2U, __ATOMIC_RELEASE);
            return;
        }
        // The bounded server cache retains any result that cannot be confirmed.
    };
    unsigned acknowledgements = 0;
    for (auto& slot : pendingAcknowledgements) {
        if (acknowledgements == 4) break;
        uint32_t ready = 2;
        if (!__atomic_compare_exchange_n(&slot.state, &ready, 1U, false,
                                         __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            continue;
        const uint64_t value = slot.id, origin = slot.thread;
        __atomic_store_n(&slot.state, 0U, __ATOMIC_RELEASE);
        ++acknowledgements;
        if (!acknowledge(value, origin)) {
            defer(value, origin);
            break;
        }
    }
    IpcMessage outgoing = request;
    const bool tagged =
        request.type != static_cast<uint64_t>(PipeOperation::Create) &&
        request.type != static_cast<uint64_t>(PipeOperation::CreateChannel);
    int64_t copied = 0;
    Ipc::Handle retained;
    if (tagged) {
        const uint32_t rights =
            request.type == static_cast<uint64_t>(PipeOperation::Read) ? 1
            : request.type == static_cast<uint64_t>(PipeOperation::Write) ||
                    request.type == static_cast<uint64_t>(PipeOperation::Eof)
                ? 2
                : 0;
        const int duplicated =
            Ipc::Handle(request.data[0], false)
                .duplicate(rights, handleFlagNoInherit | handleFlagCloseOnExec,
                           retained);
        copied =
            duplicated ? duplicated : static_cast<int64_t>(retained.getValue());
        if (copied <= 0)
            return copied ? copied : static_cast<int>(ErrorCode::InvalidHandle);
        outgoing.data[0] = copied;
    }
    // Keep the original object alive if another thread closes/reuses its FD.

    const uint32_t end = outgoing.length;
    const uint64_t id  = __atomic_add_fetch(&sequence, 1ULL, __ATOMIC_RELAXED);
    outgoing.data[end] = serviceReplayMagic;
    outgoing.data[end + 1] = id;
    outgoing.data[end + 2] = 0;
    outgoing.length += 3;
    uint64_t recoveryDeadline = 0, attemptDeadline = deadline;
    int status;
    while (true) {
        status = callOnce(outgoing, reply, attemptDeadline);
        const bool unavailable =
            status == static_cast<int>(ErrorCode::PeerDied) ||
            status == static_cast<int>(ErrorCode::EndpointClosed) ||
            status == static_cast<int>(ErrorCode::InvalidHandle) ||
            status == static_cast<int>(ErrorCode::NotFound);
        const bool interrupted =
            status == static_cast<int>(ErrorCode::Interrupted) ||
            status == static_cast<int>(ErrorCode::Timeout);
        if (!unavailable && !interrupted) break;
        const uint64_t now = Sys().readMonotonicTime();
        if (!recoveryDeadline) recoveryDeadline = now + 20000000000ULL;
        if (now >= recoveryDeadline)
            return static_cast<int>(ErrorCode::Timeout);
        if (interrupted && !outgoing.data[end + 2])
            outgoing.data[end + 2] =
                static_cast<uint64_t>(static_cast<int64_t>(status));
        attemptDeadline = recoveryDeadline;
        sleep(1);
    }
    if (!status) {
        // Once the reply is in client memory, its replay record may be retired.
        if (!acknowledge(id, thread)) defer(id, thread);
    }
    return status;
}

int64_t Sys::Transport::result(IpcMessage& request, IpcMessage& reply) {
    const auto kind  = kind_;
    const int status = call(request, reply);
    return status ? status
           : kind == CoreServiceKind::MessageQueue &&
                   request.type ==
                       static_cast<uint64_t>(MessageQueueOperation::Open) &&
                   reply.handleCount
               ? reply.handles[0]
               : static_cast<int64_t>(reply.data[0]);
}

void Sys::log(const char* message) {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(SystemOperation::Log);
    request.length = 5;
    if (!message) return;
    Runtime::copyString(reinterpret_cast<char*>(request.data), message, 512);
    request.length = 64;
    this->result(request, reply);
}

uint64_t Sys::readClockResolution() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(SystemOperation::ClockResolution);
    request.length = 5;
    return this->result(request, reply);
}

bool detail::SystemQuery::getSystemInfo(os::wire::SystemInfo& info) {
    IpcMessage request{}, reply{};
    request.type      = static_cast<uint64_t>(SystemOperation::Info);
    request.length    = 5;
    const auto status = this->result(request, reply);
    if (!status) Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    return status == 0;
}

uint64_t Sys::readMonotonicTime() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(SystemOperation::MonotonicTime);
    request.length = 5;
    return this->result(request, reply);
}

uint64_t Sys::readWallClockTime() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(SystemOperation::WallClockTime);
    request.length = 5;
    return this->result(request, reply);
}

detail::SystemQuery::SystemQuery() : Sys::Transport(CoreServiceKind::System) {}

uint64_t Sys::getUptimeNanoseconds() const { return uptimeNanoseconds_; }

uint64_t Sys::getTotalMemoryBytes() const { return totalMemoryBytes_; }

uint64_t Sys::getFreeMemoryBytes() const { return freeMemoryBytes_; }

uint32_t Sys::getOnlineCpus() const { return onlineCpus_; }

bool Sys::refresh() {
    wire::SystemInfo value{};
    if (!detail::SystemQuery().getSystemInfo(value)) return false;
    {
        uptimeNanoseconds_ = value.uptimeNanoseconds;
        totalMemoryBytes_  = value.totalMemoryBytes;
        freeMemoryBytes_   = value.freeMemoryBytes;
        onlineCpus_        = value.onlineCpus;
        reserved_          = value.reserved;
    }
    return true;
}

Sys::Transport::Transport(CoreServiceKind kind) : kind_(kind) {}

Sys::Sys() : transport_(CoreServiceKind::System) {}

Sys::Recovery::Recovery() : Sys::Transport(CoreServiceKind::Task) {}
}  // namespace os

namespace os {
int64_t Sys::result(IpcMessage& request, IpcMessage& reply) {
    return transport_.result(request, reply);
}
int Sys::Registry::lookup(const char* name, Ipc& endpoint) {
    if (endpoint.getValid()) return static_cast<int>(ErrorCode::Busy);
    const uint64_t raw = lookupRaw(name, 100, nullptr);
    if (!raw) return static_cast<int>(ErrorCode::NotFound);
    Ipc::Handle handle(raw);
    return endpoint.open(static_cast<Ipc::Handle&&>(handle));
}
int Sys::Registry::registerName(const char* name, Ipc& endpoint,
                                uint64_t& generation) {
    return registerRaw(name, endpoint.getHandle(), 0, &generation)
               ? 0
               : static_cast<int>(ErrorCode::IoError);
}
int Sys::Registry::unregisterName(const char* name, uint64_t generation) {
    return unregisterRaw(name, generation)
               ? 0
               : static_cast<int>(ErrorCode::IoError);
}
detail::RegistryClient::RegistryClient(uint64_t endpoint)
    : Sys::Registry(endpoint) {}
uint64_t detail::RegistryClient::lookup(const char* name, uint32_t attempts,
                                        uint64_t* generation) {
    return lookupRaw(name, attempts, generation);
}
bool detail::RegistryClient::registerName(const char* name, uint64_t endpoint,
                                          uint64_t access,
                                          uint64_t* generation) {
    return registerRaw(name, endpoint, access, generation);
}
bool detail::RegistryClient::unregister(const char* name, uint64_t generation) {
    return unregisterRaw(name, generation);
}
uint64_t detail::RegistryClient::endpoint() const { return getEndpoint(); }
}  // namespace os
