#include "os/abi.h"
#include "os/task.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/terminal.h"
#include "service_manager.h"

class InitSupervisor {
 public:
    int run() {
    os::System::log("[INIT] Ring 3 init started\n");
    if (!manager_.initialize()) return 1;
    registryEndpoint = manager_.endpoint();
    if (!publishCoreServices()) return 1;
    os::System::log("[INIT] service discovery online\n");
    if (!startCore(os::ProgramId::PciServer)) {
        os::System::log("[INIT] pci service startup failed\n");
        return 2;
    }
    const uint64_t pci = os::ServiceRegistry::lookupService(registryEndpoint,
                                            os::pciServiceName, 10000);
    if (pci == 0) {
        os::System::log("[INIT] pci service discovery failed\n");
        return 2;
    }
    os::closeHandle(pci);
    if (!startCore(os::ProgramId::PipeServer) || !startCore(os::ProgramId::MessageQueueServer)) return 2;
    const char* channels[] = {"pipe", "msgq"};
    for (const char* name : channels) {
        const uint64_t endpoint = os::ServiceRegistry::lookupService(registryEndpoint, name, 10000);
        if (!endpoint) return 2;
        os::closeHandle(endpoint);
    }
    if (!startCore(os::ProgramId::TaskServer) ||
        !startCore(os::ProgramId::DeviceServer) ||
        !startCore(os::ProgramId::StorageServer) ||
        !startCore(os::ProgramId::FsServer)) {
        os::System::log("[INIT] service startup failed\n");
        return 2;
    }
    os::System::log("[INIT] pci, task, device, storage and fs services started\n");
    const uint64_t fs =
        os::ServiceRegistry::lookupService(registryEndpoint, os::fsServiceName, 30000);
    if (fs == 0) {
        os::System::log("[INIT] fs service discovery failed\n");
        return 3;
    }
    os::closeHandle(fs);
    if (!startCore(os::ProgramId::MemoryManager)) return 4;
    const uint64_t mm = os::ServiceRegistry::lookupService(registryEndpoint, "mm", 10000);
    if (!mm) { os::System::log("[INIT] memory manager registration failed\n"); return 4; }
    os::closeHandle(mm);
    if (!startCore(os::ProgramId::UsersServer)) {
        os::System::log("[INIT] users service startup failed\n"); return 4;
    }
    const uint64_t users = os::ServiceRegistry::lookupService(registryEndpoint, "users", 10000);
    if (!users) { os::System::log("[INIT] users service registration failed\n"); return 4; }
    os::closeHandle(users);
    if (!startCore(os::ProgramId::TtyServer)) {
        os::System::log("[INIT] tty service startup failed\n");
        return 4;
    }
    const uint64_t tty = os::ServiceRegistry::lookupService(registryEndpoint,
                                           os::ttyServiceName, 10000);
    if (!tty) {
        os::System::log("[INIT] tty service registration failed\n");
        return 4;
    }
    os::closeHandle(tty);
    if (!startCore(os::ProgramId::ConsoleServer)) {
        os::System::log("[INIT] console service startup failed\n");
        return 4;
    }
    os::System::log("[INIT] console and virtual TTY service started\n");
    if (!startCore(os::ProgramId::WindowServer)) {
        os::System::log("[INIT] guiw service startup failed\n");
        return 5;
    }
    os::System::log("[INIT] guiw service started\n");
    if (!startDiskProgram("/usr/bin/terminal", "terminal")) {
        os::System::log("[INIT] GUI terminal startup failed\n");
        return 6;
    }
    if (!os::Terminal::switchTo(os::graphicalTerminalId)) {
        os::System::log("[INIT] unable to activate graphical terminal\n");
        return 7;
    }
    os::System::log("[INIT] GUI terminal started through PTY\n");
    for (uint32_t tty = 0; tty < os::textTerminalCount; ++tty) {
        char terminalArgument[] = "--os-terminal=1";
        terminalArgument[14]    = static_cast<char>('1' + tty);
        const char* arguments[] = {"sh", terminalArgument};
        if (os::Process::execute("/bin/sh", 2, arguments) == 0) {
            os::System::log("[INIT] TTY shell startup failed\n");
            return 8;
        }
    }
    os::System::log("[INIT] tty1-tty6 shells started\n");
    supervise();
    return 0;

    }
 private:
    class ManagedService {
     public:
        os::ProgramId program{};
        const char* path = nullptr;
        const char* name = nullptr;
        uint64_t task = 0;
        bool builtin = false;
        uint64_t start(uint64_t registry) const {
            if (builtin) return os::Process::spawnProgram(program, registry);
            const char* arguments[] = {name};
            return os::Process::execute(path, 1, arguments);
        }
    };
    ServiceManager manager_;
    uint64_t registryEndpoint = 0;
    ManagedService managed_[12]{};
    uint32_t managedCount_ = 0;
    bool startCore(os::ProgramId program) {
        if (managedCount_ >= 12) return false;
        auto& service = managed_[managedCount_];
        service.program = program; service.builtin = true;
        service.task = service.start(registryEndpoint);
        if (!service.task) return false;
        ++managedCount_;
        return true;
    }
    bool startDiskProgram(const char* path, const char* name) {
        if (managedCount_ >= 12) {
            const char* arguments[] = {name};
            return os::Process::execute(path, 1, arguments) != 0;
        }
        auto& service = managed_[managedCount_];
        service.path = path; service.name = name;
        service.task = service.start(registryEndpoint);
        if (!service.task) return false;
        ++managedCount_;
        return true;
    }
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
    void supervise() {
        while (true) {
            for (uint32_t i = 0; i < managedCount_; ++i) {
                auto& service = managed_[i];
                int code = 0;
                if (service.task && os::Process::getTaskState(service.task, code) == 0) continue;
                if (service.task) os::closeHandle(service.task);
                service.task = service.start(registryEndpoint);
            }
            int status = 0;
            while (os::Process::waitChild(-1, status, 1) > 0) {}
            os::sleep(1000);
        }
    }
};

int main(int, char**) { static InitSupervisor supervisor; return supervisor.run(); }
