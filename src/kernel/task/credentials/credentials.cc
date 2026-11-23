/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/credentials/credentials.cc
 * 查询和修改任务身份、附加组、能力及认证会话。
 */

#include "kernel/task.h"
#include "service/common/records.h"

namespace task {

    /*
     * 在任务锁内复制真实、有效、保存的用户和组身份以及附加组，给调用方一份完整身份快照。
     */
    void Task::credentials(services::wire::CredentialInfo& info) {
        InterruptSpinLockGuard guard(lock_);
        info = {uid_,          effectiveUid_, savedUid_,           gid_,
                effectiveGid_, savedGid_,     supplementaryCount_, {}};

        for (uint32_t i = 0; i < supplementaryCount_; ++i) {
            info.groups[i] = supplementaryGroups_[i];
        }
    }

    /*
     * 按操作编号修改用户、组或附加组身份。
     * 先根据旧身份和特权检查允许的变化，再统一提交；身份提升后限制转储，避免泄露敏感数据。
     */
    int Task::changeCredentials(unsigned operation, uint32_t first,
                                uint32_t second, uint32_t third,
                                const uint32_t* groups) {
        InterruptSpinLockGuard guard(lock_);

        if (state_ == TaskState::Terminated) {
            return 3;
        }

        const bool privileged = effectiveUid_ == 0;
        // 修改附加组需要特权；先验证整个输入列表，再替换旧组，失败时保留原身份。
        if (operation == 9) {
            if (!privileged) {
                return 1;
            }

            if (first > services::maximumSupplementaryGroups || (first && !groups)) {
                return 22;
            }

            for (uint32_t i = 0; i < first; ++i) {
                if (groups[i] == UINT32_MAX) {
                    return 22;
                }
            }

            for (uint32_t i = 0; i < first; ++i) {
                supplementaryGroups_[i] = groups[i];
            }

            supplementaryCount_ = first;
            dumpable_           = false;

            return 0;
        }

        if (!operation || operation > 8) {
            return 22;
        }

        const bool user = operation == 1 || operation == 3 || operation == 5 ||
                          operation == 7;
        const uint32_t oldReal      = user ? uid_ : gid_,
                       oldEffective = user ? effectiveUid_ : effectiveGid_,
                       oldSaved     = user ? savedUid_ : savedGid_;
        uint32_t real = oldReal, effective = oldEffective, saved = oldSaved;

        struct IdentityPermission {
                const bool& privileged;
                const uint32_t& oldReal;
                const uint32_t& oldEffective;
                const uint32_t& oldSaved;

                /* 普通任务只允许在已有身份之间切换，UINT32_MAX
                 * 表示保持该项不变。 */
                bool operator()(uint32_t id) const {
                    return privileged || id == UINT32_MAX || id == oldReal ||
                           id == oldEffective || id == oldSaved;
                }
        } const allowed{privileged, oldReal, oldEffective, oldSaved};

        // root 的 setuid/setgid
        // 可以同时改变三类身份，普通调用者只能按规则切回真实或保存身份。
        if (operation <= 2) {
            if (first == UINT32_MAX) {
                return 22;
            }

            if (privileged) {
                real = effective = saved = first;
            } else if (first == oldReal || first == oldSaved) {
                effective = first;
            } else {
                return 1;
            }

            // 两身份操作分别检查真实与有效身份；修改真实身份或把有效身份改离真实身份时还要更新保存身份。
        } else if (operation <= 4) {
            if (!privileged && first != UINT32_MAX && first != oldReal &&
                first != oldEffective) {
                return 1;
            }

            if (!allowed(second)) {
                return 1;
            }

            if (first != UINT32_MAX) {
                real = first;
            }

            if (second != UINT32_MAX) {
                effective = second;
            }

            if (first != UINT32_MAX ||
                (second != UINT32_MAX && second != oldReal)) {
                saved = effective;
            }

            // 三身份操作先验证每一项，全部通过才提交，避免某一项失败却已修改其他字段。
        } else if (operation <= 6) {
            if (!allowed(first) || !allowed(second) || !allowed(third)) {
                return 1;
            }

            if (first != UINT32_MAX) {
                real = first;
            }

            if (second != UINT32_MAX) {
                effective = second;
            }

            if (third != UINT32_MAX) {
                saved = third;
            }
        } else {
            if (first == UINT32_MAX) {
                return 22;
            }

            if (!allowed(first)) {
                return 1;
            }

            effective = first;
        }

        // 身份变化后收紧普通核心转储资格；真实身份改变时，旧认证会话信息也随之失效。
        if (real != oldReal || effective != oldEffective || saved != oldSaved) {
            dumpable_ = false;
        }

        if (real != oldReal) {
            authenticationSession_ = 0;
        }

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

    /*
     * 向任务增加能力位。
     * 使用按位合并保留已有能力，修改由任务锁保护。
     */
    void Task::grantCapabilities(uint64_t capabilities) {
        capabilities_ |= capabilities;
    }

    /*
     * 检查任务是否包含所需能力位，供内核服务执行权限校验。
     */
    bool Task::hasCapability(uint64_t capability) const {
        return (capabilities_ & capability) == capability;
    }

    /*
     * 读取认证会话和认证时确认的用户、组，与当前可变的执行身份分别记录。
     */
    void Task::authentication(uint64_t& session, uint32_t& uid, uint32_t& gid) {
        InterruptSpinLockGuard guard(lock_);
        session = authenticationSession_;
        uid     = authenticationUid_;
        gid     = authenticationGid_;
    }

    /*
     * 登记认证成功的身份和附加组。
     * 按参数决定是否保留当前执行身份，同时更新会话认证信息。
     */
    bool Task::authenticate(uint32_t uid, uint32_t gid,
                            bool preserveCredentials, uint32_t groupCount,
                            const uint32_t* groups) {
        InterruptSpinLockGuard guard(lock_);

        if (state_ == TaskState::Terminated || managedService_ ||
            uid >= 65535 || gid >= 65535 ||
            groupCount > services::maximumSupplementaryGroups ||
            (groupCount && !groups) ||
            (preserveCredentials &&
             (effectiveUid_ != uid || effectiveGid_ != gid))) {
            return false;
        }

        // 附加组内容先完整验证，避免开始修改身份以后才发现列表中有非法编号。
        for (uint32_t i = 0; i < groupCount; ++i) {
            if (groups[i] == UINT32_MAX) {
                return false;
            }
        }

        if (!preserveCredentials) {
            if (uid_ != uid || effectiveUid_ != uid || gid_ != gid ||
                effectiveGid_ != gid) {
                dumpable_ = false;
            }

            uid_ = effectiveUid_ = savedUid_ = uid;
            gid_ = effectiveGid_ = savedGid_ = gid;
            supplementaryCount_              = groupCount;

            for (uint32_t i = 0; i < groupCount; ++i) {
                supplementaryGroups_[i] = groups[i];
            }
        }

        // 认证信息与当前执行身份分开保存，保留凭据模式下不应意外覆盖已有有效身份。
        authenticationSession_ = sessionId_;
        authenticationUid_     = uid;
        authenticationGid_     = gid;

        return true;
    }

    /*
     * 设置任务用户和组身份；可只更新有效身份。
     * 修改身份后同步调整相关安全状态。
     */
    void Task::setCredentials(uint32_t uid, uint32_t gid, bool effectiveOnly) {
        InterruptSpinLockGuard guard(lock_);

        if (uid != effectiveUid_ || gid != effectiveGid_) {
            dumpable_ = false;
        }

        effectiveUid_ = uid;
        effectiveGid_ = gid;

        if (!effectiveOnly) {
            if (uid_ != uid || gid_ != gid) {
                authenticationSession_ = 0;
            }

            uid_      = uid;
            gid_      = gid;
            savedUid_ = uid;
            savedGid_ = gid;
        }
    }

}  // namespace task
