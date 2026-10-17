#include "os/abi.h"
#include "os/task.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/terminal.h"

namespace {

struct ServiceRecord {
    char name[os::serviceNameCapacity];
    uint64_t endpoint;
};

uint64_t registryEndpoint;
ServiceRecord services[12];
uint32_t serviceCount;

void registryMain() {
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(registryEndpoint, request) != 0) {
            continue;
        }
        os::IpcMessage reply{};
        const char* name = reinterpret_cast<const char*>(request.data);
        if (request.type ==
                static_cast<uint64_t>(os::RegistryRequest::Register) &&
            request.handleCount == 1 && serviceCount < 12) {
            bool replaced = false;
            bool denied = false;
            for (uint32_t index = 0; index < serviceCount; ++index) {
                if (os::stringsEqual(services[index].name, name)) {
                    if (os::stringsEqual(name, os::pciServiceName)) {
                        os::closeHandle(request.handles[0]);
                        denied = true;
                        replaced = true;
                        break;
                    }
                    services[index].endpoint = request.handles[0];
                    replaced                 = true;
                    break;
                }
            }
            if (!replaced) {
                os::copyString(services[serviceCount].name, name,
                               os::serviceNameCapacity);
                services[serviceCount].endpoint = request.handles[0];
                ++serviceCount;
            }
            reply.data[0] = denied ? 2 : 0;
        } else if (request.type ==
                   static_cast<uint64_t>(os::RegistryRequest::Lookup)) {
            reply.data[0] = 1;
            for (uint32_t index = 0; index < serviceCount; ++index) {
                if (os::stringsEqual(services[index].name, name)) {
                    reply.data[0]     = 0;
                    reply.handleCount = 1;
                    reply.handles[0]  = services[index].endpoint;
                    break;
                }
            }
        } else {
            reply.data[0] = 2;
        }
        reply.length = 1;
        os::replyMessage(registryEndpoint, reply);
    }
}

bool startDiskProgram(const char* path, const char* name) {
    const char* arguments[] = {name};
    return os::execute(path, 1, arguments) != 0;
}

}  // namespace

int main(int, char**) {
    os::log("[INIT] Ring 3 init started\n");
    registryEndpoint                     = os::createEndpoint();
    constexpr uint64_t registryStackBase = 0x0000000070000000ULL;
    constexpr uint64_t registryStackSize = 4 * 4096;
    if (registryEndpoint == 0 ||
        os::mapMemory(registryStackBase, registryStackSize, 1) !=
            registryStackBase ||
        os::createThread(registryMain, registryStackBase + registryStackSize) ==
            0) {
        os::log("[INIT] unable to start service registry\n");
        return 1;
    }
    os::setServiceRegistry(registryEndpoint);
    os::log("[INIT] service discovery online\n");
    if (os::spawnProgram(os::ProgramId::PciServer, registryEndpoint) == 0) {
        os::log("[INIT] pci service startup failed\n");
        return 2;
    }
    const uint64_t pci = os::lookupService(registryEndpoint,
                                            os::pciServiceName, 10000);
    if (pci == 0) {
        os::log("[INIT] pci service discovery failed\n");
        return 2;
    }
    os::closeHandle(pci);
    if (os::spawnProgram(os::ProgramId::TaskServer, registryEndpoint) == 0 ||
        os::spawnProgram(os::ProgramId::DeviceServer, registryEndpoint) == 0 ||
        os::spawnProgram(os::ProgramId::StorageServer, registryEndpoint) == 0 ||
        os::spawnProgram(os::ProgramId::FsServer, registryEndpoint) == 0) {
        os::log("[INIT] service startup failed\n");
        return 2;
    }
    os::log("[INIT] pci, task, device, storage and fs services started\n");
    const uint64_t fs =
        os::lookupService(registryEndpoint, os::fsServiceName, 30000);
    if (fs == 0) {
        os::log("[INIT] fs service discovery failed\n");
        return 3;
    }
    os::closeHandle(fs);
    if (!startDiskProgram("/sbin/sys/users.elf", "users")) {
        os::log("[INIT] users service startup failed\n"); return 4;
    }
    const uint64_t users = os::lookupService(registryEndpoint, "users", 10000);
    if (!users) { os::log("[INIT] users service registration failed\n"); return 4; }
    os::closeHandle(users);
    if (!startDiskProgram("/sbin/sys/tty.elf", "tty")) {
        os::log("[INIT] tty service startup failed\n");
        return 4;
    }
    const uint64_t tty = os::lookupService(registryEndpoint,
                                           os::ttyServiceName, 10000);
    if (!tty) {
        os::log("[INIT] tty service registration failed\n");
        return 4;
    }
    os::closeHandle(tty);
    if (!startDiskProgram("/sbin/sys/console.elf", "console")) {
        os::log("[INIT] console service startup failed\n");
        return 4;
    }
    os::log("[INIT] console and virtual TTY service started\n");
    if (!startDiskProgram("/sbin/sys/guiw.elf", "guiw")) {
        os::log("[INIT] guiw service startup failed\n");
        return 5;
    }
    os::log("[INIT] guiw service started\n");
    if (!startDiskProgram("/usr/bin/terminal", "terminal")) {
        os::log("[INIT] GUI terminal startup failed\n");
        return 6;
    }
    if (!os::switchTerminal(os::graphicalTerminalId)) {
        os::log("[INIT] unable to activate graphical terminal\n");
        return 7;
    }
    os::log("[INIT] GUI terminal started through PTY\n");
    for (uint32_t tty = 0; tty < os::textTerminalCount; ++tty) {
        char terminalArgument[] = "--os-terminal=1";
        terminalArgument[14]    = static_cast<char>('1' + tty);
        const char* arguments[] = {"sh", terminalArgument};
        if (os::execute("/bin/sh", 2, arguments) == 0) {
            os::log("[INIT] TTY shell startup failed\n");
            return 8;
        }
    }
    os::log("[INIT] tty1-tty6 shells started\n");
    while (true) {
        os::sleep(1000);
    }
}
