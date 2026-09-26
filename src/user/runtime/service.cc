#include "os/service.h"

#include "os/abi.h"
#include "os/protocol.h"

namespace {

uint64_t configuredRegistry;

void putName(os::IpcMessage& message, const char* name) {
    char* target = reinterpret_cast<char*>(message.data);
    os::copyString(target, name, sizeof(message.data));
    message.length = 8;
}

}  // namespace

namespace os {

void setServiceRegistry(uint64_t endpoint) { configuredRegistry = endpoint; }

uint64_t serviceRegistry() {
    return configuredRegistry != 0 ? configuredRegistry : getBootstrapHandle();
}

bool registerService(uint64_t registry, const char* name, uint64_t endpoint) {
    IpcMessage request{};
    request.type        = static_cast<uint64_t>(RegistryRequest::Register);
    request.handleCount = 1;
    request.handles[0]  = endpoint;
    putName(request, name);
    IpcMessage reply{};
    return callService(registry, request, reply) == 0 && reply.data[0] == 0;
}

uint64_t lookupService(uint64_t registry, const char* name, uint32_t attempts) {
    for (uint32_t attempt = 0; attempt < attempts; ++attempt) {
        IpcMessage request{};
        request.type = static_cast<uint64_t>(RegistryRequest::Lookup);
        putName(request, name);
        IpcMessage reply{};
        if (callService(registry, request, reply) == 0 && reply.data[0] == 0 &&
            reply.handleCount == 1) {
            return reply.handles[0];
        }
        sleep(1);
    }
    return 0;
}

}  // namespace os
