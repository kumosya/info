#pragma once

#include "directories.h"
#include "os/task.h"

namespace users {
class SessionManager {
   public:
    explicit SessionManager(DirectoryManager& directories) : directories_(directories) {}
    bool begin(uint64_t task, const os::UserProfile& profile) {
        sweep();
        Session* vacant = nullptr;
        for (auto& session : sessions_) {
            if (session.task == task) {
                if (session.uid == profile.uid) return directories_.runtime(profile);
                const uint32_t previous = session.uid;
                session = {};
                releaseIfUnused(previous);
            }
            if (!session.task && !vacant) vacant = &session;
        }
        if (!vacant || !directories_.runtime(profile)) return false;
        *vacant = {task, profile.uid};
        return true;
    }
    void sweep() {
        for (auto& session : sessions_) {
            if (!session.task) continue;
            os::TaskInfo task{};
            if (!os::Process::queryTask(session.task, task) && task.state != 2 &&
                task.effectiveUid == session.uid) continue;
            const uint32_t uid = session.uid;
            session = {};
            releaseIfUnused(uid);
        }
    }
   private:
    struct Session { uint64_t task = 0; uint32_t uid = 0; } sessions_[64];
    DirectoryManager& directories_;
    void releaseIfUnused(uint32_t uid) {
        for (const auto& session : sessions_) if (session.task && session.uid == uid) return;
        if (!directories_.removeRuntime(uid))
            os::System::log("[USERS] runtime directory cleanup failed\n");
    }
};
}  // namespace users
