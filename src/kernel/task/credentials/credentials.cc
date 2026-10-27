/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/credentials/credentials.cc
 * Process credentials, capability bits, and authenticated identities.
 */

#include "kernel/task.h"
#include "service/common/records.h"

namespace kernel::task {

// Copy real/effective/saved IDs and supplemental groups under one task lock so
// the wire reply does not combine fields from different updates.
void Task::credentials(os::wire::CredentialInfo& info) {
    InterruptSpinLockGuard guard(lock_);
    info = {uid_,          effectiveUid_, savedUid_,           gid_,
            effectiveGid_, savedGid_,     supplementaryCount_, {}};
    for (uint32_t i = 0; i < supplementaryCount_; ++i)
        info.groups[i] = supplementaryGroups_[i];
}
// Apply one current-process POSIX credential operation atomically. The service
// decodes operation and arguments; this method enforces identity transition
// rules and returns positive errno (0 on success). UINT32_MAX means unchanged
// only in multi-ID forms, not in a single-ID assignment.
int Task::changeCredentials(unsigned operation, uint32_t first, uint32_t second,
                            uint32_t third, const uint32_t* groups) {
    InterruptSpinLockGuard guard(lock_);
    if (state_ == TaskState::Terminated) return 3;
    const bool privileged = effectiveUid_ == 0;
    // Supplemental group replacement is privileged. Validate the whole input
    // before changing the stored list, preserving the old groups on failure.
    if (operation == 9) {
        if (!privileged) return 1;
        if (first > os::maximumSupplementaryGroups || (first && !groups))
            return 22;
        for (uint32_t i = 0; i < first; ++i)
            if (groups[i] == UINT32_MAX) return 22;
        for (uint32_t i = 0; i < first; ++i)
            supplementaryGroups_[i] = groups[i];
        supplementaryCount_ = first;
        dumpable_           = false;
        return 0;
    }
    if (!operation || operation > 8) return 22;
    const bool user =
        operation == 1 || operation == 3 || operation == 5 || operation == 7;
    const uint32_t oldReal      = user ? uid_ : gid_,
                   oldEffective = user ? effectiveUid_ : effectiveGid_,
                   oldSaved     = user ? savedUid_ : savedGid_;
    uint32_t real = oldReal, effective = oldEffective, saved = oldSaved;
    const auto allowed = [&](uint32_t id) {
        return privileged || id == UINT32_MAX || id == oldReal ||
               id == oldEffective || id == oldSaved;
    };
    // setuid/setgid changes all three IDs for root; unprivileged callers may
    // only switch the effective ID back to a real or saved identity.
    if (operation <= 2) {
        if (first == UINT32_MAX) return 22;
        if (privileged)
            real = effective = saved = first;
        else if (first == oldReal || first == oldSaved)
            effective = first;
        else
            return 1;
        // Two-ID changes have distinct real/effective checks. Updating the real
        // ID or changing effective away from real also refreshes the saved
        // identity.
    } else if (operation <= 4) {
        if (!privileged && first != UINT32_MAX && first != oldReal &&
            first != oldEffective)
            return 1;
        if (!allowed(second)) return 1;
        if (first != UINT32_MAX) real = first;
        if (second != UINT32_MAX) effective = second;
        if (first != UINT32_MAX || (second != UINT32_MAX && second != oldReal))
            saved = effective;
        // Three-ID forms authorize each requested field before committing any,
        // so a rejected component cannot leave partially changed credentials.
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
    // Credential changes invalidate ordinary core-dump eligibility. Changing
    // the real identity also invalidates the prior authenticated session
    // record.
    if (real != oldReal || effective != oldEffective || saved != oldSaved)
        dumpable_ = false;
    if (real != oldReal) authenticationSession_ = 0;
    if (user) {
        uid_          = real;
        effectiveUid_ = effective;
        savedUid_     = saved;
    } else {
        gid_          = real;
        effectiveGid_ = effective;
        savedGid_     = saved;
    }
    return 0;
}

// Add capability bits from an already authorized loader/service path; this
// primitive does not authenticate an untrusted requester itself.
void Task::grantCapabilities(uint64_t capabilities) {
    capabilities_ |= capabilities;
}

// Require every requested capability bit, not merely a nonzero overlap.
bool Task::hasCapability(uint64_t capability) const {
    return (capabilities_ & capability) == capability;
}

// Authenticated session number and identity, distinct from mutable process
// credentials; take all three fields under the same lock.
void Task::authentication(uint64_t& session, uint32_t& uid, uint32_t& gid) {
    InterruptSpinLockGuard guard(lock_);
    session = authenticationSession_;
    uid     = authenticationUid_;
    gid     = authenticationGid_;
}

// Install the account service's authenticated identity on an ordinary live
// task. preserveCredentials requires matching effective IDs and leaves the
// credential tuple intact; otherwise replace IDs and supplemental groups. The
// task service checks that the caller is the authorized Users service.
bool Task::authenticate(uint32_t uid, uint32_t gid, bool preserveCredentials,
                        uint32_t groupCount, const uint32_t* groups) {
    InterruptSpinLockGuard guard(lock_);
    if (state_ == TaskState::Terminated || managedService_ || uid >= 65535 ||
        gid >= 65535 || groupCount > os::maximumSupplementaryGroups ||
        (groupCount && !groups) ||
        (preserveCredentials && (effectiveUid_ != uid || effectiveGid_ != gid)))
        return false;
    for (uint32_t i = 0; i < groupCount; ++i)
        if (groups[i] == UINT32_MAX) return false;
    if (!preserveCredentials) {
        if (uid_ != uid || effectiveUid_ != uid || gid_ != gid ||
            effectiveGid_ != gid)
            dumpable_ = false;
        uid_ = effectiveUid_ = savedUid_ = uid;
        gid_ = effectiveGid_ = savedGid_ = gid;
        supplementaryCount_              = groupCount;
        for (uint32_t i = 0; i < groupCount; ++i)
            supplementaryGroups_[i] = groups[i];
    }
    authenticationSession_ = sessionId_;
    authenticationUid_     = uid;
    authenticationGid_     = gid;
    return true;
}

// Low-level authorized credential assignment; effectiveOnly preserves real and
// saved IDs. Caller permissions are checked by the service before this method.
// Identity changes invalidate dumpability and, where applicable, the
// previously authenticated session.
void Task::setCredentials(uint32_t uid, uint32_t gid, bool effectiveOnly) {
    InterruptSpinLockGuard guard(lock_);
    if (uid != effectiveUid_ || gid != effectiveGid_) dumpable_ = false;
    effectiveUid_ = uid;
    effectiveGid_ = gid;
    if (!effectiveOnly) {
        if (uid_ != uid || gid_ != gid) authenticationSession_ = 0;
        uid_      = uid;
        gid_      = gid;
        savedUid_ = uid;
        savedGid_ = gid;
    }
}

}  // namespace kernel::task
