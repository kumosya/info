#include "os/abi.h"
#include "os/task.h"
#include "os/service.h"
#include "os/input.h"

int main() {
    const auto registry = os::ServiceRegistry::serviceRegistry();
    const auto input = os::ServiceRegistry::lookupService(registry, os::inputServiceName, 1000);
    const auto blocked = os::Endpoint::create();
    if (!input || !blocked) return 1;
    os::IpcMessage request{}, reply{}, notification{};
    request.type = static_cast<uint64_t>(os::InputRequest::Subscribe);
    request.handleCount = 1; request.handles[0] = blocked;
    if (os::callService(input, request, reply) || reply.data[0]) return 2;
    unsigned queued = 0;
    while (!os::sendMessage(blocked, notification, 0)) if (++queued > 1024) return 3;
    os::System::log("[INPUT PRESSURE] subscriber queue full\n");
    // Keep a live consumer that deliberately does not receive notifications.
    // Other subscribers and the IRQ input worker must continue independently.
    os::sleep(40000);
    os::closeHandle(blocked); os::closeHandle(input);
    return 0;
}
