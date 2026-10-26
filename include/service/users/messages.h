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
struct UserProfile {
    uint32_t uid, gid;
    char name[32], home[96], shell[64];
};
struct UserCredentials {
    uint32_t id;
    char name[32], password[128], replacement[128];
};
static_assert(sizeof(UserProfile) == 200);
static_assert(sizeof(UserCredentials) == 292);
}  // namespace os::wire
