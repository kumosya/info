#include "service/common/channel.h"
#pragma once
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
namespace os {
struct SessionIdentityInfo {
    uint64_t session;
    uint32_t uid, gid;
};
class SessionIdentity {
   public:
    static int query(uint64_t task, SessionIdentityInfo& identity) {
        IpcMessage request{}, reply{};
        request.type     = static_cast<uint64_t>(TaskOperation::Authentication);
        request.length   = 4;
        request.data[0]  = task;
        const int result = services::RequestChannel(CoreServiceKind::Task)
                               .result(request, reply);
        if (!result)
            Runtime::copyBytes(&identity, &reply.data[1], sizeof(identity));
        return result;
    }
    static int establish(uint64_t task, uint32_t uid, uint32_t gid,
                         bool preserveCredentials = false,
                         uint32_t groupCount      = 0,
                         const uint32_t* groups   = nullptr) {
        if (groupCount > maximumSupplementaryGroups || (groupCount && !groups))
            return static_cast<int>(ErrorCode::InvalidArgument);
        IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(TaskOperation::AuthenticateSession);
        request.length  = 5 + groupCount;
        request.data[0] = task;
        request.data[1] = uid;
        request.data[2] = gid;
        request.data[3] = preserveCredentials;
        request.data[4] = groupCount;
        for (uint32_t i = 0; i < groupCount; ++i)
            request.data[5 + i] = groups[i];
        return services::RequestChannel(CoreServiceKind::Task)
            .result(request, reply);
    }
};
}  // namespace os
