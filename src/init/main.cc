#include <os/console.h>
#include <os/detail/client.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>

#include "service/common/lifecycle.h"
#include "service/common/protocol.h"
#include "service/init/init.h"

namespace services {

#include "startup.inc"
}

namespace init {

    class InitSupervisor {
            InitServiceManager manager_;
            services::Lifecycle services_;
            SessionSupervisor sessions_;
            BootReporter boot_;
            bool background_ = false;
            uint64_t registryEndpoint = 0;

            static void serviceChanged(
                void* context, const services::wire::ServiceStatus& status) {
                auto& self = *static_cast<InitSupervisor*>(context);
                self.boot_.record(
                    status.name,
                    status.state == os::Sys::Status::State::Running,
                    status.exitCode);
                if (!self.background_) {
                    self.boot_.pump(self.registryEndpoint);
                }
            }

            void collectChildServices() {
                for (const auto& unit : services::configuredServices) {
                    if (os::Runtime::stringsEqual(unit.owner, "init")) {
                        continue;
                    }

                    char manager[24] = "control.";
                    os::Runtime::copyString(manager + 8, unit.owner,
                                            sizeof(manager) - 8);
                    const uint64_t endpoint =
                        os::detail::RegistryClient(registryEndpoint)
                            .lookup(manager, 1);
                    if (!endpoint) {
                        continue;
                    }

                    os::IpcMessage request{}, reply{};
                    request.type = services::serviceControlMessage;
                    request.length = 5;
                    request.data[0] = static_cast<uint64_t>(
                        services::ServiceOperation::Status);
                    os::Runtime::copyString(
                        reinterpret_cast<char*>(&request.data[2]), unit.name,
                        24);
                    if (!os::callService(
                            endpoint, request, reply,
                            os::Sys().readMonotonicTime() + 250000000ULL) &&
                        !reply.data[0] &&
                        reply.length >=
                            1 + (sizeof(services::wire::ServiceStatus) + 7) /
                                    8) {
                        services::wire::ServiceStatus status;
                        os::Runtime::copyBytes(&status, &reply.data[1],
                                               sizeof(status));
                        boot_.record(
                            unit.name,
                            status.state == os::Sys::Status::State::Running,
                            status.exitCode);
                    }

                    os::closeHandle(endpoint);
                }
            }

            bool publishCoreServices() {
                if (!os::detail::RegistryClient(registryEndpoint)
                         .registerName("services", registryEndpoint)) {
                    return false;
                }

                const char* names[] = {"pipe",          "msgq",
                                       "memory.kernel", "device.kernel",
                                       "system.kernel", "task.kernel",
                                       "pci.kernel"};
                for (uint32_t index = 0;
                     index <
                     static_cast<uint32_t>(services::CoreServiceKind::Count);
                     ++index) {
                    if (index == static_cast<uint32_t>(
                                     services::CoreServiceKind::Pipe) ||
                        index == static_cast<uint32_t>(
                                     services::CoreServiceKind::MessageQueue) ||
                        index == static_cast<uint32_t>(
                                     services::CoreServiceKind::Ipc)) {
                        continue;
                    }

                    const uint64_t endpoint =
                        os::detail::RequestChannel(
                            static_cast<services::CoreServiceKind>(index))
                            .openHandle();
                    if (!endpoint) {
                        return false;
                    }

                    const bool registered =
                        os::detail::RegistryClient(registryEndpoint)
                            .registerName(names[index], endpoint);
                    os::closeHandle(endpoint);
                    if (!registered) {
                        return false;
                    }
                }

                return true;
            }

        public:
            int run() {
                os::Sys().log("[INIT] Ring 3 init started\n");
                if (!manager_.init()) {
                    return 1;
                }

                registryEndpoint = manager_.endpoint();
                if (!publishCoreServices()) {
                    return 2;
                }

                os::Sys().log("[INIT] service discovery online\n");
                boot_.record("init", true);
                services_.observe(serviceChanged, this);
                if (!services_.init("init", registryEndpoint) ||
                    !services_.bootstrap()) {
                    return 3;
                }

                const bool configurationReady = services_.load();
                if (!configurationReady) {
                    os::Sys().log(
                        "[INIT] file service configuration rejected; bootstrap "
                        "control "
                        "remains available\n");
                }

                boot_.record("service configuration", configurationReady);
                collectChildServices();
                background_ = true;
                if (!services_.supervise()) {
                    return 5;
                }

                os::Sys().log("[INIT] service lifecycle control online\n");
                const auto console =
                    os::detail::RegistryClient(registryEndpoint)
                        .lookup("console", 1);
                if (console) {
                    os::closeHandle(console);
                    os::Sys().log(
                        "[INIT] console and virtual TTY service started\n");
                }

                const auto window = os::detail::RegistryClient(registryEndpoint)
                                        .lookup("guiw", 1);
                if (window) {
                    os::closeHandle(window);
                    os::Sys().log("[INIT] guiw service started\n");
                }

                if (!sessions_.init()) {
                    os::Sys().log(
                        "[INIT] session startup incomplete; monitoring "
                        "available "
                        "services\n");
                }

                for (uint32_t i = 0; i <= os::graphicalTerminalId; ++i) {
                    char name[] = "tty1 session";
                    name[3] = '1' + i;
                    boot_.record(name, sessions_.available(i));
                }

                boot_.pump(registryEndpoint,
                           sessions_.initialTerminal(boot_.preferredTerminal()),
                           sessions_.initialTerminal(0));
                while (true) {
                    sessions_.tick();
                    boot_.pump(
                        registryEndpoint,
                        sessions_.initialTerminal(boot_.preferredTerminal()),
                        sessions_.initialTerminal(0));
                    os::sleep(1000);
                }
            }
    };
}  // namespace init

int main(int, char**) {
    static init::InitSupervisor supervisor;
    return supervisor.run();
}
