#ifndef _OS_CXX_USERS_H
#define _OS_CXX_USERS_H
#include "abi.h"
#include "service.h"

namespace os {
enum class UserOperation : uint64_t { Lookup = 1, Login, Sudo, Password, Who, Add };
struct UserProfile {
    uint32_t uid, gid;
    char name[32], home[96], shell[64];
};
struct UserCredentials {
    uint32_t id;
    char name[32], password[128], replacement[128];
};
class UserClient {
   public:
    static int request(UserOperation operation, const UserCredentials& credentials, UserProfile& profile) {
        const uint64_t service = ServiceRegistry::lookupService(ServiceRegistry::serviceRegistry(), "users", 2000);
        if (!service) return -1;
        IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(operation);
        Runtime::copyBytes(request.data, &credentials, sizeof(credentials));
        request.length = (sizeof(credentials) + 7) / 8;
        const int status = callService(service, request, reply);
        closeHandle(service);
        if (status || reply.length < 1 || reply.data[0]) return -1;
        if (reply.length >= 1 + (sizeof(profile) + 7) / 8) Runtime::copyBytes(&profile, &reply.data[1], sizeof(profile));
        return 0;
    }
};
}
#endif
