#include "os/abi.h"
#include "os/task.h"
#include "os/protocol.h"
#include "os/service.h"
#include "service_manager.h"
#include "../service/lifecycle.h"
#include "sessions.h"

class InitSupervisor {
    ServiceManager manager_;
    services::Lifecycle services_;
    SessionSupervisor sessions_;
    uint64_t registryEndpoint = 0;
    bool publishCoreServices() {
        if (!os::ServiceRegistry::registerService(registryEndpoint, "services", registryEndpoint)) return false;
        const char* names[] = {"pipe", "msgq", "memory.kernel", "device.kernel", "system.kernel", "task.kernel", "pci.kernel"};
        for (uint32_t index = 0; index < static_cast<uint32_t>(os::CoreServiceKind::Count); ++index) {
            if (index == static_cast<uint32_t>(os::CoreServiceKind::Pipe) || index == static_cast<uint32_t>(os::CoreServiceKind::MessageQueue)) continue;
            const uint64_t endpoint = os::CoreServiceClient::openHandle(static_cast<os::CoreServiceKind>(index));
            if (!endpoint) return false;
            const bool registered = os::ServiceRegistry::registerService(registryEndpoint, names[index], endpoint);
            os::closeHandle(endpoint);
            if (!registered) return false;
        }
        return true;
    }
 public:
    int run() {
        os::System::log("[INIT] Ring 3 init started\n");
        if (!manager_.initialize()) return 1;
        registryEndpoint = manager_.endpoint();
        if (!publishCoreServices()) return 2;
        os::System::log("[INIT] service discovery online\n");
        if (!services_.initialize("init", registryEndpoint) || !services_.bootstrap()) return 3;
        if (!services_.load()) os::System::log("[INIT] file service configuration rejected; bootstrap control remains available\n");
        if (!services_.supervise()) return 5;
        os::System::log("[INIT] service lifecycle control online\n");
        const auto console = os::ServiceRegistry::lookupService(registryEndpoint, "console", 1);
        if (console) { os::closeHandle(console); os::System::log("[INIT] console and virtual TTY service started\n"); }
        const auto window = os::ServiceRegistry::lookupService(registryEndpoint, "guiw", 1);
        if (window) { os::closeHandle(window); os::System::log("[INIT] guiw service started\n"); }
        if (!sessions_.initialize()) os::System::log("[INIT] session startup incomplete; monitoring available services\n");
        while (true) { sessions_.tick(); os::sleep(1000); }
    }
};
int main(int, char**) { static InitSupervisor supervisor; return supervisor.run(); }
