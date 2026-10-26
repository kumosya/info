#include <os/user.h>

#include "service/common/runtime.h"
#include "service/users/messages.h"

namespace os {
namespace {
class UserEncoding {
   public:
    void erase(void* pointer, size_t count) {
        auto* bytes = static_cast<volatile unsigned char*>(pointer);
        while (count--) *bytes++ = 0;
    }
    bool terminated(const char* text, size_t size) {
        for (size_t i = 0; i < size; ++i)
            if (!text[i]) return true;
        return false;
    }
    bool copyInput(char* target, const char* source, size_t size) {
        if (!source) return true;
        for (size_t i = 0; i < size; ++i) {
            target[i] = source[i];
            if (!source[i]) return true;
        }
        return false;
    }
};
class SecretMessage {
   public:
    explicit SecretMessage(IpcMessage& message) : message_(message) {}
    ~SecretMessage() { UserEncoding().erase(&message_, sizeof(message_)); }

   private:
    IpcMessage& message_;
};
}  // namespace
uint32_t User::Profile::getUid() const { return uid_; }
uint32_t User::Profile::getGid() const { return gid_; }
const char* User::Profile::getName() const { return name_; }
const char* User::Profile::getHome() const { return home_; }
const char* User::Profile::getShell() const { return shell_; }
User::User() = default;
int User::open() {
    if (connection_.getConnected()) return static_cast<int>(ErrorCode::Busy);
    return connection_.connect("users") ? 0
                                        : static_cast<int>(ErrorCode::NotFound);
}
int User::close() {
    connection_.disconnect();
    return connection_.getConnected() ? static_cast<int>(ErrorCode::IoError)
                                      : 0;
}
int User::exchange(uint64_t operation, uint32_t id, const char* name,
                   const char* password, const char* replacement,
                   User::Profile& profile, uint64_t* sessionTask) {
    if (!connection_.getConnected() && !connection_.connect("users")) return 2;
    IpcMessage request{}, reply{};
    SecretMessage cleanup(request);
    UserEncoding encoding;
    wire::UserCredentials input{};
    input.id = id;
    const bool valid =
        encoding.copyInput(input.name, name, sizeof(input.name)) &&
        encoding.copyInput(input.password, password, sizeof(input.password)) &&
        encoding.copyInput(input.replacement, replacement,
                           sizeof(input.replacement));
    if (valid) Runtime::copyBytes(request.data, &input, sizeof(input));
    encoding.erase(&input, sizeof(input));
    if (!valid) return 2;
    request.type     = operation;
    request.length   = (sizeof(input) + 7) / 8;
    const int status = connection_.call(
        request, reply, Sys().readMonotonicTime() + 10000000000ULL);
    for (uint32_t i = 0; i < reply.handleCount && i < 4; ++i)
        closeHandle(reply.handles[i]);
    if (status || reply.length < 1) return 2;
    if (reply.data[0]) return 1;
    const size_t expected =
        sessionTask ? 41 : 1 + (sizeof(wire::UserProfile) + 7) / 8;
    if (reply.length != expected || reply.handleCount) return 2;
    wire::UserProfile value{};
    Runtime::copyBytes(&value, &reply.data[1], sizeof(value));
    if (!encoding.terminated(value.name, sizeof(value.name)) ||
        !encoding.terminated(value.home, sizeof(value.home)) ||
        !encoding.terminated(value.shell, sizeof(value.shell)))
        return 2;
    profile.uid_ = value.uid;
    profile.gid_ = value.gid;
    Runtime::copyBytes(profile.name_, value.name, sizeof(value.name));
    Runtime::copyBytes(profile.home_, value.home, sizeof(value.home));
    Runtime::copyBytes(profile.shell_, value.shell, sizeof(value.shell));
    if (sessionTask) *sessionTask = reply.data[40];
    return 0;
}
int User::lookup(uint32_t uid, User::Profile& profile) {
    return exchange(uint64_t(wire::UserOperation::Lookup), uid, nullptr,
                    nullptr, nullptr, profile);
}
int User::lookupName(const char* name, User::Profile& profile) {
    if (!name || !*name) return 2;
    return exchange(uint64_t(wire::UserOperation::Lookup), 0, name, nullptr,
                    nullptr, profile);
}
int User::login(const char* name, const char* password,
                User::Profile& profile) {
    if (!name || !*name) return 2;
    return exchange(uint64_t(wire::UserOperation::Login), 0, name, password,
                    nullptr, profile);
}
int User::authorizeSudo(const char* password, User::Profile& profile) {
    return exchange(uint64_t(wire::UserOperation::Sudo), 0, nullptr, password,
                    nullptr, profile);
}
int User::changePassword(const char* name, const char* password,
                         const char* replacement) {
    Profile profile;
    if (!name || !*name) return 2;
    return exchange(uint64_t(wire::UserOperation::Password), 0, name, password,
                    replacement, profile);
}
int User::querySession(uint32_t taskId, User::Profile& profile,
                       uint64_t& sessionTask) {
    return exchange(uint64_t(wire::UserOperation::Who), taskId, nullptr,
                    nullptr, nullptr, profile, &sessionTask);
}
int User::beginSession(uint32_t uid, User::Profile& profile) {
    return exchange(uint64_t(wire::UserOperation::BeginSession), uid, nullptr,
                    nullptr, nullptr, profile);
}
int User::autoLogin(User::Profile& profile) {
    return exchange(uint64_t(wire::UserOperation::AutoLogin), 0, nullptr,
                    nullptr, nullptr, profile);
}
int User::list(uint32_t index, User::Profile& profile) {
    return exchange(uint64_t(wire::UserOperation::List), index, nullptr,
                    nullptr, nullptr, profile);
}
int User::graphicalLogin(const char* name, const char* password,
                         User::Profile& profile) {
    if (!name || !*name) return 2;
    return exchange(uint64_t(wire::UserOperation::GraphicalLogin), 0, name,
                    password, nullptr, profile);
}
}  // namespace os
