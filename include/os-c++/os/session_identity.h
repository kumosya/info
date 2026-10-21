#pragma once
#include "os/core_service.h"
namespace os {
struct SessionIdentityInfo { uint64_t session; uint32_t uid, gid; };
class SessionIdentity {
   public:
    static int query(uint64_t task, SessionIdentityInfo& identity) {
        IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(TaskOperation::Authentication); request.length = 4; request.data[0] = task;
        const int result = CoreServiceClient::result(CoreServiceKind::Task, request, reply);
        if (!result) Runtime::copyBytes(&identity, &reply.data[1], sizeof(identity));
        return result;
    }
    static int establish(uint64_t task, uint32_t uid, uint32_t gid, bool preserveCredentials = false) {
        IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(TaskOperation::AuthenticateSession); request.length = 4;
        request.data[0] = task; request.data[1] = uid; request.data[2] = gid; request.data[3] = preserveCredentials;
        return CoreServiceClient::result(CoreServiceKind::Task, request, reply);
    }
};
}
