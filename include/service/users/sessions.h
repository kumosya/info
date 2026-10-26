#include <os/dir.h>
#include "service/users/messages.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#pragma once

#include "service/users/directories.h"
#include <os/task.h>
#include <os/file.h>
#include "service/users/identity.h"
#include "service/users/accounts.h"

namespace users {
class SessionManager {
   public:
    SessionManager(DirectoryManager& directories, Accounts& accounts) : directories_(directories), accounts_(accounts) {}
    bool init() { return refresh(true); }
    bool prepare(uint64_t task, const os::wire::UserProfile& profile) {
        (void)task;
        return refresh(true) && directories_.runtime(profile);
    }
    void commit(uint64_t task, const os::wire::UserProfile& profile) {
        (void)task;
        for (uint32_t i = 0; i < accounts_.count(); ++i) if (accounts_.at(i).profile.uid == profile.uid) active_[i] = true;
        nextSweep_ = os::Sys().readMonotonicTime() + 1000000000ULL;
    }
    void cancel(const os::wire::UserProfile& profile) {
        for (uint32_t i = 0; i < accounts_.count(); ++i)
            if (accounts_.at(i).profile.uid == profile.uid && !active_[i]) directories_.removeRuntime(profile.uid);
    }
    bool begin(uint64_t task, const os::wire::UserProfile& profile) {
        if (!prepare(task, profile)) return false;
        if (os::SessionIdentity::establish(task, profile.uid, profile.gid, true)) { cancel(profile); return false; }
        commit(task, profile); return true;
    }
    void sweep() { refresh(false); }
   private:
    DirectoryManager& directories_;
    Accounts& accounts_;
    bool active_[Accounts::capacity]{};
    bool initialized_ = false;
    uint64_t nextSweep_ = 0;
    bool refresh(bool force) {
        const auto now = os::Sys().readMonotonicTime();
        if (!force && now < nextSweep_) return true;
        bool active[Accounts::capacity]{};
        os::Dir proc;os::Dir::Entry entries[8];if(proc.open("/proc"))return false;
        bool complete = false;
        for (unsigned batch = 0; batch < 1024; ++batch) {
            size_t count=0;if(proc.readBatch(entries,8,count))return false;
            if (!count) { complete = true; break; }
            for (ssize_t n = 0; n < count; ++n) {
                uint64_t pid = 0; const char* p = entries[n].getName();
                while (*p >= '0' && *p <= '9') { pid = pid * 10 + *p - '0'; ++p; }
                if (!pid || *p) continue;
                os::wire::TaskInfo info{}; os::SessionIdentityInfo identity{};
                if (os::detail::TaskQuery().query(pid, info) || info.state == 2 || os::SessionIdentity::query(pid, identity) || !identity.session) continue;
                for (uint32_t i = 0; i < accounts_.count(); ++i) if (accounts_.at(i).profile.uid == identity.uid) active[i] = true;
            }
        }
        if (!complete) return false;
        for (uint32_t i = 0; i < accounts_.count(); ++i) {
            const auto& profile = accounts_.at(i).profile;
            if (active[i] && (!initialized_ || !active_[i]) && !directories_.runtime(profile)) return false;
            if (!active[i] && (!initialized_ || active_[i]) && !directories_.removeRuntime(profile.uid)) return false;
        }
        for (uint32_t i = 0; i < accounts_.count(); ++i) active_[i] = active[i];
        initialized_ = true; nextSweep_ = now + 1000000000ULL; return true;
    }
};
}  // namespace users
