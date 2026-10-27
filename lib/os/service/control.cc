#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include <os/mm.h>
#include <os/service.h>

namespace os {
Sys::Manager::Manager(const char* name) {
    Runtime::copyString(manager_, name ? name : "control.init",
                        sizeof(manager_));
}

int Sys::Manager::call(ServiceOperation operation, const char* name,
                       uint64_t index, wire::ServiceStatus& status) {
    const uint64_t endpoint =
        os::detail::RegistryClient(os::detail::RegistryClient().endpoint())
            .lookup(manager_, 1000);
    if (!endpoint) return static_cast<int>(ErrorCode::NotFound);
    IpcMessage request{}, reply{};
    request.type    = serviceControlMessage;
    request.length  = 5;
    request.data[0] = static_cast<uint64_t>(operation);
    request.data[1] = index;
    if (name)
        Runtime::copyString(reinterpret_cast<char*>(&request.data[2]), name,
                            24);
    const int result = callService(endpoint, request, reply,
                                   Sys().readMonotonicTime() + 65000000000ULL);
    closeHandle(endpoint);
    if (result) return result;
    if (!reply.length) return static_cast<int>(ErrorCode::IoError);
    if (!reply.data[0] &&
        (operation == ServiceOperation::List ||
         operation == ServiceOperation::Status) &&
        reply.length < 1 + (sizeof(status) + 7) / 8)
        return static_cast<int>(ErrorCode::IoError);
    if (reply.length >= 1 + (sizeof(status) + 7) / 8)
        Runtime::copyBytes(&status, &reply.data[1], sizeof(status));
    return static_cast<int>(reply.data[0]);
}

}  // namespace os

namespace os {
detail::ManagerClient::ManagerClient(const char* name) : Sys::Manager(name) {}
int Sys::Manager::list(uint32_t index, Status& status) {
    return queryResult(ServiceOperation::List, nullptr, index, status);
}
int Sys::Manager::query(const char* name, Status& status) {
    return queryResult(ServiceOperation::Status, name, 0, status);
}
int Sys::Manager::queryResult(ServiceOperation operation, const char* name,
                              uint64_t index, Status& status) {
    wire::ServiceStatus value{};
    const int result = call(operation, name, index, value);
    if (result) return result;
    if (static_cast<uint32_t>(value.state) > 3)
        return static_cast<int>(ErrorCode::IoError);
    Runtime::copyString(status.name_, value.name, sizeof(status.name_));
    Runtime::copyString(status.owner_, value.owner, sizeof(status.owner_));
    Runtime::copyString(status.executable_, value.executable,
                        sizeof(status.executable_));
    Runtime::copyString(status.blockedBy_, value.blockedBy,
                        sizeof(status.blockedBy_));
    status.taskId_       = value.pid;
    status.state_        = value.state;
    status.enabled_      = value.enabled;
    status.desired_      = value.desired;
    status.builtin_      = value.builtin;
    status.restartCount_ = value.restarts;
    status.exitCode_     = value.exitCode;
    return 0;
}
int Sys::Manager::update(uint64_t operation, const char* name) {
    wire::ServiceStatus ignored{};
    return call(static_cast<ServiceOperation>(operation), name, 0, ignored);
}
int Sys::Manager::start(const char* name) {
    return update(static_cast<uint64_t>(ServiceOperation::Start), name);
}
int Sys::Manager::stop(const char* name) {
    return update(static_cast<uint64_t>(ServiceOperation::Stop), name);
}
int Sys::Manager::restart(const char* name) {
    return update(static_cast<uint64_t>(ServiceOperation::Restart), name);
}
int Sys::Manager::enable(const char* name) {
    return update(static_cast<uint64_t>(ServiceOperation::Enable), name);
}
int Sys::Manager::disable(const char* name) {
    return update(static_cast<uint64_t>(ServiceOperation::Disable), name);
}
const char* Sys::Status::getName() const { return name_; }
const char* Sys::Status::getOwner() const { return owner_; }
const char* Sys::Status::getExecutable() const { return executable_; }
const char* Sys::Status::getBlockedBy() const { return blockedBy_; }
uint64_t Sys::Status::getTaskId() const { return taskId_; }
Sys::Status::State Sys::Status::getState() const { return state_; }
bool Sys::Status::getEnabled() const { return enabled_; }
bool Sys::Status::getDesired() const { return desired_; }
bool Sys::Status::getBuiltin() const { return builtin_; }
uint32_t Sys::Status::getRestartCount() const { return restartCount_; }
int32_t Sys::Status::getExitCode() const { return exitCode_; }
}  // namespace os
