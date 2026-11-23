/*
 * 用户服务请求和认证数据。
 */

#pragma once
#include <cstdint>
namespace os::wire {
    // Fixed users-service message layout. Public user objects are encoded
    // explicitly.
    enum class UserOperation : uint64_t {
        Lookup = 1,
        Login,
        Sudo,
        Password,
        Who,
        Add,
        BeginSession,
        AutoLogin,
        List,
        GraphicalLogin
    };
    /* 保存用户编号、组编号、名称、主目录及登录程序等账户资料。 */
    struct UserProfile {
            uint32_t uid, gid;
            char name[32], home[96], shell[64];
    };
    /* 保存认证请求携带的账户名与凭据字段，使用时应遵守消息边界。 */
    struct UserCredentials {
            uint32_t id;
            char name[32], password[128], replacement[128];
    };
    static_assert(sizeof(UserProfile) == 200);
    static_assert(sizeof(UserCredentials) == 292);
}  // namespace os::wire
