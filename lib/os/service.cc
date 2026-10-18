#include "os/service.h"

#include "os/abi.h"
#include "os/protocol.h"

namespace os {
uint64_t ServiceRegistry::configuredRegistry_;
bool ServiceRegistry::putName(os::IpcMessage& message, const char* name) {
    if (!name || !name[0] || Runtime::stringLength(name) >= serviceNameCapacity) return false;
    char* target = reinterpret_cast<char*>(message.data);
    os::Runtime::copyString(target, name, serviceNameCapacity);
    message.length = 5;
    return true;
}


int SystemService::pciRequest(PciRequest&) { return static_cast<int>(ErrorCode::PermissionDenied); }
int SystemService::queryTask(uint64_t, TaskInfo&) { return static_cast<int>(ErrorCode::PermissionDenied); }
int SystemService::coreCall(CoreServiceKind, IpcMessage&, IpcMessage&, uint64_t) {
    return static_cast<int>(ErrorCode::PermissionDenied);
}

void ServiceRegistry::setServiceRegistry(uint64_t endpoint) { configuredRegistry_ = endpoint; }

uint64_t ServiceRegistry::serviceRegistry() {
    return configuredRegistry_ != 0 ? configuredRegistry_ : getBootstrapHandle();
}

bool ServiceRegistry::registerService(uint64_t registry, const char* name, uint64_t endpoint,
                                      uint64_t access, uint64_t* generation) {
    IpcMessage request{};
    request.type        = static_cast<uint64_t>(RegistryRequest::Register);
    request.handleCount = 1;
    request.handles[0]  = endpoint;
    request.handleOptions[0] = ipcHandleRestrictRights;
    request.handleRights[0] = 2;
    if (!ServiceRegistry::putName(request, name)) return false;
    request.data[3] = access;
    IpcMessage reply{};
    const bool registered = callService(registry, request, reply) == 0 && reply.data[0] == 0;
    if (registered && generation) *generation = reply.data[1];
    return registered;
}

uint64_t ServiceRegistry::lookupService(uint64_t registry, const char* name, uint32_t attempts, uint64_t* generation) {
    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        IpcMessage request{};
        request.type = static_cast<uint64_t>(RegistryRequest::Lookup);
        if (!ServiceRegistry::putName(request, name)) return 0;
        IpcMessage reply{};
        if (callService(registry, request, reply) == 0 && reply.data[0] == 0 &&
            reply.handleCount == 1) {
            if (generation) *generation = reply.data[1];
            return reply.handles[0];
        }
        sleep(1);
    }
    return 0;
}

bool ServiceRegistry::unregisterService(uint64_t registry, const char* name, uint64_t generation) {
    IpcMessage request{}, reply{};
    if (!putName(request, name)) return false;
    request.type = static_cast<uint64_t>(RegistryRequest::Unregister);
    request.data[4] = generation;
    return callService(registry, request, reply) == 0 && reply.data[0] == 0;
}

}  // namespace os
