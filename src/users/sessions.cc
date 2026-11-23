/* 服务对象的成员函数实现。 */
#include "service/users/users.h"

namespace users {

    /* 确认登录会话并登记活动用户。 */
    void SessionManager::commit(uint64_t task,
                                const services::wire::UserProfile& profile) {
        (void)task;
        for (uint32_t i = 0; i < accounts_.count(); ++i) {
            if (accounts_.at(i).profile.uid == profile.uid) {
                active_[i] = true;
            }
        }

        nextSweep_ = os::Sys().readMonotonicTime() + 1000000000ULL;
    }

    /* 撤销用户会话准备并清理运行目录。 */
    void SessionManager::cancel(const services::wire::UserProfile& profile) {
        for (uint32_t i = 0; i < accounts_.count(); ++i) {
            if (accounts_.at(i).profile.uid == profile.uid && !active_[i]) {
                directories_.removeRuntime(profile.uid);
            }
        }
    }

    /* 准备目录并建立认证会话。 */
    bool SessionManager::begin(uint64_t task,
                               const services::wire::UserProfile& profile) {
        if (!prepare(task, profile)) {
            return false;
        }

        if (os::SessionIdentity(task).establish(profile.uid, profile.gid,
                                                true)) {
            cancel(profile);
            return false;
        }

        commit(task, profile);
        return true;
    }

    /* 扫描进程中的认证会话并清理退出用户。 */
    bool SessionManager::refresh(bool force) {
        const uint64_t now = os::Sys().readMonotonicTime();
        if (!force && now < nextSweep_) {
            return true;
        }

        bool active[Accounts::capacity]{};
        os::Dir proc;
        os::Dir::Entry entries[8];
        if (proc.open("/proc")) {
            return false;
        }

        bool complete = false;
        for (unsigned batch = 0; batch < 1024; ++batch) {
            size_t count = 0;
            if (proc.readBatch(entries, 8, count)) {
                return false;
            }

            if (!count) {
                complete = true;
                break;
            }

            for (size_t n = 0; n < count; ++n) {
                uint64_t pid = 0;
                const char* p = entries[n].getName();
                while (*p >= '0' && *p <= '9') {
                    pid = pid * 10 + *p - '0';
                    ++p;
                }

                if (!pid || *p) {
                    continue;
                }

                services::wire::TaskInfo info{};
                os::SessionIdentityInfo identity{};
                if (os::detail::TaskQuery().query(pid, info) ||
                    info.state == 2 ||
                    os::SessionIdentity(pid).query(identity) ||
                    !identity.session) {
                    continue;
                }

                for (uint32_t i = 0; i < accounts_.count(); ++i) {
                    if (accounts_.at(i).profile.uid == identity.uid) {
                        active[i] = true;
                    }
                }
            }
        }

        if (!complete) {
            return false;
        }

        for (uint32_t i = 0; i < accounts_.count(); ++i) {
            const services::wire::UserProfile& profile =
                accounts_.at(i).profile;
            if (active[i] && (!initialized_ || !active_[i]) &&
                !directories_.runtime(profile)) {
                return false;
            }

            if (!active[i] && (!initialized_ || active_[i]) &&
                !directories_.removeRuntime(profile.uid)) {
                return false;
            }
        }

        for (uint32_t i = 0; i < accounts_.count(); ++i) {
            active_[i] = active[i];
        }

        initialized_ = true;
        nextSweep_ = now + 1000000000ULL;
        return true;
    }

}  // namespace users
