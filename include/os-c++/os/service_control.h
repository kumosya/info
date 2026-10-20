#pragma once
#include "os/task.h"
#include "os/service.h"
namespace os {
enum class ServiceOperation : uint64_t { List = 1, Status, Start, Stop, Restart, Enable, Disable };
enum class ServiceState : uint32_t { Stopped, Running, Failed, Starting };
constexpr uint64_t serviceControlMessage = 0x535643;
struct ServiceStatus {
    char name[24]{}, owner[24]{}, executable[128]{};
    uint64_t pid = 0;
    ServiceState state{};
    uint32_t enabled = 0, desired = 0, builtin = 0, restarts = 0;
    int32_t exitCode = 0;
    char blockedBy[24]{};
};
class ServiceControl {
 public:
    static int call(ServiceOperation operation, const char* name, uint64_t index, ServiceStatus& status,
                    const char* manager = "control.init") {
        const uint64_t endpoint = ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), manager, 1000);
        if (!endpoint) return static_cast<int>(ErrorCode::NotFound);
        IpcMessage request{}, reply{}; request.type = serviceControlMessage; request.length = 5;
        request.data[0] = static_cast<uint64_t>(operation); request.data[1] = index;
        if (name) Runtime::copyString(reinterpret_cast<char*>(&request.data[2]), name, 24);
        const int result = callService(endpoint, request, reply, Clock::getMonotonicTime() + 65000000000ULL);
        closeHandle(endpoint);
        if (result) return result;
        if (reply.length >= 1 + (sizeof(status) + 7) / 8)
            Runtime::copyBytes(&status, &reply.data[1], sizeof(status));
        return static_cast<int>(reply.data[0]);
    }
};
}
