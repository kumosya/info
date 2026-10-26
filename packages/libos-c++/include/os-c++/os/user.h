#pragma once
#include <os/service.h>
namespace os {
class User {
   public:
    class Profile {
       public:
        uint32_t getUid() const;
        uint32_t getGid() const;
        const char* getName() const;
        const char* getHome() const;
        const char* getShell() const;

       private:
        friend class User;
        uint32_t uid_{}, gid_{};
        char name_[32]{}, home_[96]{}, shell_[64]{};
    };
    User();
    int open();
    int close();
    // Zero means success, one means rejection, two means transport/protocol
    // failure.
    int lookup(uint32_t uid, Profile& profile);
    int lookupName(const char* name, Profile& profile);
    int login(const char* name, const char* password, Profile& profile);
    int authorizeSudo(const char* password, Profile& profile);
    int changePassword(const char* name, const char* password,
                       const char* replacement);
    int querySession(uint32_t taskId, Profile& profile, uint64_t& sessionTask);
    int beginSession(uint32_t uid, Profile& profile);
    int autoLogin(Profile& profile);
    int list(uint32_t index, Profile& profile);
    int graphicalLogin(const char* name, const char* password,
                       Profile& profile);

   private:
    Sys::Connection connection_;
    int exchange(uint64_t operation, uint32_t id, const char* name,
                 const char* password, const char* replacement,
                 Profile& profile, uint64_t* sessionTask = nullptr);
};
}  // namespace os
