/*
 * 用户身份与认证。
 */

#pragma once
#include <os/service.h>

namespace os {

    /* 封装用户查询、认证与会话身份操作。 */
    class User {
        public:
            /* 保存用户公开资料，供用户服务查询和展示。 */
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
            // Zero means success, one means rejection, two means
            // transport/protocol failure.
            int lookup(uint32_t uid, Profile& profile);
            int lookupName(const char* name, Profile& profile);
            int login(const char* name, const char* password, Profile& profile);
            int authorizeSudo(const char* password, Profile& profile);
            int changePassword(const char* name, const char* password,
                               const char* replacement);
            int querySession(uint32_t taskId, Profile& profile,
                             uint64_t& sessionTask);
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

    /* 保存一次登录会话认证出的用户和组信息。 */
    struct SessionIdentityInfo {
            uint64_t session;
            uint32_t uid, gid;
    };

    /* 维护登录会话已经认证的身份，区分会话身份与进程当前身份。 */
    class SessionIdentity {
        public:
            explicit SessionIdentity(uint64_t task);
            ~SessionIdentity() = default;
            int query(SessionIdentityInfo& identity);
            int establish(uint32_t uid, uint32_t gid,
                          bool preserveCredentials = false,
                          uint32_t groupCount = 0,
                          const uint32_t* groups = nullptr);

            uint64_t getTask() const {
                return task_;
            }

            uint64_t getSession() const {
                return identity_.session;
            }

            uint32_t getUserId() const {
                return identity_.uid;
            }

            uint32_t getGroupId() const {
                return identity_.gid;
            }

        private:
            uint64_t task_;
            SessionIdentityInfo identity_{};
            Sys::Transport channel_;
    };

    /* 清理一个已结束的认证会话所创建的用户进程。 */
    class SessionCleanup {
        public:
            explicit SessionCleanup(uint64_t session);
            ~SessionCleanup() = default;
            void terminate();

            uint64_t getSession() const {
                return session_;
            }

        private:
            uint64_t session_;
    };

}  // namespace os
