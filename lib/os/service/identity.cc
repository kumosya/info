#include <os/detail/client.h>
#include <os/runtime.h>

#include "service/common/protocol.h"
/* 服务对象的成员函数实现。 */
#include <os/user.h>

namespace os {

    /* 连接对应任务的认证身份，并复用任务服务通道。 */
    SessionIdentity::SessionIdentity(uint64_t task)
        : task_(task), channel_(services::CoreServiceKind::Task) {
    }

    /* 查询目标任务的认证会话及用户身份。 */
    int SessionIdentity::query(SessionIdentityInfo& identity) {
        IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(services::TaskOperation::Authentication);
        request.length = 4;
        request.data[0] = task_;
        const int result = channel_.result(request, reply);
        if (!result) {
            if (reply.length < 1 + sizeof(identity_) / sizeof(uint64_t)) {
                return static_cast<int>(ErrorCode::IoError);
            }

            Runtime::copyBytes(&identity_, &reply.data[1], sizeof(identity_));
            identity = identity_;
        }

        return result;
    }

    /* 向任务服务登记经过认证的用户与附加组。 */
    int SessionIdentity::establish(uint32_t uid, uint32_t gid,
                                   bool preserveCredentials,
                                   uint32_t groupCount,
                                   const uint32_t* groups) {
        if (groupCount > services::maximumSupplementaryGroups ||
            (groupCount && !groups)) {
            return static_cast<int>(ErrorCode::InvalidArgument);
        }

        IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(services::TaskOperation::AuthenticateSession);
        request.length = 5 + groupCount;
        request.data[0] = task_;
        request.data[1] = uid;
        request.data[2] = gid;
        request.data[3] = preserveCredentials;
        request.data[4] = groupCount;
        for (uint32_t i = 0; i < groupCount; ++i) {
            request.data[5 + i] = groups[i];
        }

        return channel_.result(request, reply);
    }

}  // namespace os
