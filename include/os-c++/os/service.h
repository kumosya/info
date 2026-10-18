#ifndef _OS_SERVICE_H
#define _OS_SERVICE_H

#include <stdint.h>

namespace os {
struct PciRequest;
struct TaskInfo;
struct IpcMessage;
enum class CoreServiceKind : uint64_t;

class SystemService {
   public:
    static int pciRequest(PciRequest& request);
    static int queryTask(uint64_t id, TaskInfo& info);
    static int coreCall(CoreServiceKind kind, IpcMessage& request, IpcMessage& reply, uint64_t deadline);
    static bool isPrivileged() {
        uint16_t selector;
        asm volatile("mov %%cs, %0" : "=r"(selector));
        return (selector & 3) == 0;
    }
    static int readPort8(uint16_t port, uint8_t& value) {
        if (!isPrivileged()) return -1;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return 0;
    }
    static int writePort8(uint16_t port, uint8_t value) {
        if (!isPrivileged()) return -1;
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
        return 0;
    }
};

class ServiceRegistry {
   public:
    static uint64_t lookupService(uint64_t registry, const char* name,
                       uint32_t attempts = 100, uint64_t* generation = nullptr);
    static bool registerService(uint64_t registry, const char* name, uint64_t endpoint,
                                 uint64_t access = 0, uint64_t* generation = nullptr);
    static bool unregisterService(uint64_t registry, const char* name, uint64_t generation);
    static uint64_t serviceRegistry();
    static void setServiceRegistry(uint64_t endpoint);
   private:
    static uint64_t configuredRegistry_;
    static bool putName(IpcMessage& message, const char* name);
};

}  // namespace os

#endif
