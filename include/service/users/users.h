/* 账户集合、用户目录与登录会话管理。 */

#pragma once
#include <dirent.h>
#include <os/detail/client.h>
#include <os/dir.h>
#include <os/file.h>
#include <os/runtime.h>
#include <os/task.h>
#include <os/user.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "service/common/protocol.h"
#include "service/common/records.h"
#include "service/users/password.h"

namespace users {

    /* 保存一个账户的公开资料和认证相关记录。 */
    class Account {
        public:
            Account() = default;
            ~Account() = default;

            const services::wire::UserProfile& getProfile() const {
                return profile;
            }

            const char* getPasswordRecord() const {
                return hash;
            }

            bool hasShadow() const {
                return shadow;
            }

        private:
            friend class Accounts;
            friend class UserService;
            friend class SessionManager;
            services::wire::UserProfile profile{};
            char hash[128]{};
            bool shadow = false;
    };

    /* 加载、查询和保存账户集合，并校验名称及身份编号。 */
    class Accounts {
        public:
            Accounts() = default;
            ~Accounts() = default;
            static constexpr uint32_t capacity = 32;
            bool load(const char* passwd = "/etc/passwd",
                      const char* shadow = "/etc/shadow");

            uint32_t count() const {
                return count_;
            }

            Account& at(uint32_t index) {
                return entries_[index];
            }

            Account* find(const char* name, uint32_t uid);
            Account* automatic();

        private:
            Account entries_[capacity]{};
            uint32_t count_ = 0;
            bool loaded_ = false;
            void clear();
            static bool name(const char* value);
            static bool number(const char* value, uint32_t& result);
            bool row(char* line, bool shadow);
            bool read(const char* path, bool shadow);
    };

    /* 处理用户目录的创建、归属及访问条件。 */
    class DirectoryManager {
        public:
            DirectoryManager() = default;
            ~DirectoryManager() = default;

            bool isInitialized() const {
                return initialized_;
            }

            bool init();

            bool prepare(const services::wire::UserProfile& profile);

            bool runtime(const services::wire::UserProfile& profile);

            bool removeRuntime(uint32_t uid);

        private:
            bool initialized_ = false;

            static bool join(char* result, size_t capacity, const char* parent,
                             const char* name) {
                return std::snprintf(result, capacity, "%s/%s", parent, name) <
                       static_cast<int>(capacity);
            }

            static bool ensure(const char* path, mode_t mode, uid_t uid,
                               gid_t gid);
            static bool failure(const char* operation, const char* path);

            static bool childName(const char* name) {
                return *name && strcmp(name, ".") && strcmp(name, "..") &&
                       !strchr(name, '/');
            }

            static bool merge(const char* source, const char* target,
                              uint32_t depth);
            static bool removeTree(const char* path, uint32_t depth);
    };

    /* 登记用户登录会话，处理会话创建、查询和退出。 */
    class SessionManager {
        public:
            SessionManager(DirectoryManager& directories, Accounts& accounts)
                : directories_(directories), accounts_(accounts) {
            }

            bool init() {
                return refresh(true);
            }

            bool prepare(uint64_t task,
                         const services::wire::UserProfile& profile) {
                (void)task;
                return refresh(true) && directories_.runtime(profile);
            }

            void commit(uint64_t task,
                        const services::wire::UserProfile& profile);
            void cancel(const services::wire::UserProfile& profile);
            bool begin(uint64_t task,
                       const services::wire::UserProfile& profile);

            void sweep() {
                refresh(false);
            }

        private:
            DirectoryManager& directories_;
            Accounts& accounts_;
            bool active_[Accounts::capacity]{};
            bool initialized_ = false;
            uint64_t nextSweep_ = 0;
            bool refresh(bool force);
    };
}  // namespace users
