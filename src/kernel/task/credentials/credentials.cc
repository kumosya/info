#include "service/common/records.h"
#include "kernel/task.h"
namespace kernel {
void Task::credentials(os::wire::CredentialInfo& info) {
    InterruptSpinLockGuard guard(lock_);
    info = {uid_, effectiveUid_, savedUid_, gid_, effectiveGid_, savedGid_, supplementaryCount_, {}};
    for (uint32_t i = 0; i < supplementaryCount_; ++i) info.groups[i] = supplementaryGroups_[i];
}
int Task::changeCredentials(unsigned operation, uint32_t first, uint32_t second, uint32_t third, const uint32_t* groups) {
    InterruptSpinLockGuard guard(lock_);
    if (state_ == TaskState::Terminated) return 3;
    const bool privileged = effectiveUid_ == 0;
    if (operation == 9) {
        if (!privileged) return 1;
        if (first > os::maximumSupplementaryGroups || (first && !groups)) return 22;
        for (uint32_t i = 0; i < first; ++i) if (groups[i] == UINT32_MAX) return 22;
        for (uint32_t i = 0; i < first; ++i) supplementaryGroups_[i] = groups[i];
        supplementaryCount_ = first; dumpable_ = false; return 0;
    }
    if (!operation || operation > 8) return 22;
    const bool user = operation == 1 || operation == 3 || operation == 5 || operation == 7;
    const uint32_t oldReal = user ? uid_ : gid_, oldEffective = user ? effectiveUid_ : effectiveGid_, oldSaved = user ? savedUid_ : savedGid_;
    uint32_t real = oldReal, effective = oldEffective, saved = oldSaved;
    const auto allowed = [&](uint32_t id) { return privileged || id == UINT32_MAX || id == oldReal || id == oldEffective || id == oldSaved; };
    if (operation <= 2) {
        if (first == UINT32_MAX) return 22;
        if (privileged) real = effective = saved = first;
        else if (first == oldReal || first == oldSaved) effective = first;
        else return 1;
    } else if (operation <= 4) {
        if (!privileged && first != UINT32_MAX && first != oldReal && first != oldEffective) return 1;
        if (!allowed(second)) return 1;
        if (first != UINT32_MAX) real = first;
        if (second != UINT32_MAX) effective = second;
        if (first != UINT32_MAX || (second != UINT32_MAX && second != oldReal)) saved = effective;
    } else if (operation <= 6) {
        if (!allowed(first) || !allowed(second) || !allowed(third)) return 1;
        if (first != UINT32_MAX) real = first;
        if (second != UINT32_MAX) effective = second;
        if (third != UINT32_MAX) saved = third;
    } else {
        if (first == UINT32_MAX) return 22;
        if (!allowed(first)) return 1;
        effective = first;
    }
    if (real != oldReal || effective != oldEffective || saved != oldSaved) dumpable_ = false;
    if (real != oldReal) authenticationSession_ = 0;
    if (user) { uid_ = real; effectiveUid_ = effective; savedUid_ = saved; }
    else { gid_ = real; effectiveGid_ = effective; savedGid_ = saved; }
    return 0;
}
}
