#include "os/abi.h"
#include "os/pci.h"
#include "os/protocol.h"
#include "os/service.h"

namespace {
class PciServer {
 public:


uint64_t endpoint;

void respond(const os::IpcMessage& request, os::IpcMessage& reply) {
    os::PciRequest command{};
    reply.data[0] = 1;
    reply.length = 1;
    os::TaskInfo caller{};
    if (request.senderPid == 0 ||
        os::SystemService::queryTask(request.senderPid, caller) != 0 ||
        caller.trustedDeviceService == 0) return;
    if (request.type == static_cast<uint64_t>(os::PciServiceRequest::Enumerate)) {
        command.operation = os::PciOperation::Enumerate;
        command.capacity = request.data[0] > 64 ? 64 :
                           static_cast<uint32_t>(request.data[0]);
        command.functions = reinterpret_cast<os::PciFunctionInfo*>(&reply.data[2]);
        const int result = os::SystemService::pciRequest(command);
        if (result >= 0) {
            reply.data[0] = 0;
            reply.data[1] = static_cast<uint64_t>(result);
            reply.length = 2 + (static_cast<uint32_t>(result) *
                                   sizeof(os::PciFunctionInfo) + 7) / 8;
        }
        return;
    }
    if (request.length < 3) return;
    os::Runtime::copyBytes(&command.function, &request.data[1], sizeof(command.function));
    command.offset = static_cast<uint16_t>(request.data[0]);
    if (request.type == static_cast<uint64_t>(os::PciServiceRequest::ReadConfig)) {
        command.operation = os::PciOperation::ReadConfig;
    } else if (request.type ==
               static_cast<uint64_t>(os::PciServiceRequest::WriteConfig)) {
        if (request.length < 4) return;
        command.operation = os::PciOperation::WriteConfig;
        command.value = static_cast<uint32_t>(request.data[3]);
    } else {
        return;
    }
    const int result = os::SystemService::pciRequest(command);
    if (result == 0) {
        reply.data[0] = 0;
        reply.data[1] = command.value;
        reply.length = 2;
    }
}


int run() {
    const uint64_t registry = os::getBootstrapHandle();
    endpoint = os::Endpoint::create();
    if (registry == 0 || endpoint == 0 ||
        !os::ServiceRegistry::registerService(registry, os::pciServiceName, endpoint)) {
        os::System::log("[PCI] service registration failed\n");
        return 1;
    }
    os::System::log("[PCI] service ready\n");
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(endpoint, request) != 0) continue;
        os::IpcMessage reply{};
        respond(request, reply);
        os::replyMessage(endpoint, request.replyToken, reply);
    }
}
};
}  // namespace

int main(int, char**) {
    static PciServer server;
    return server.run();
}
