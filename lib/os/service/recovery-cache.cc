/* 服务对象的成员函数实现。 */
#include "service/common/recovery.h"

namespace services {

    /* 登记本轮仍在使用的恢复令牌。 */
    bool RecoveryCapabilities::keep(uint64_t token) {
        if (!token) {
            return true;
        }

        const uint32_t index = static_cast<uint32_t>(token) - 1;
        if (index >= capacity) {
            return false;
        }

        entries_[index] = {token, visit_};
        return true;
    }

    /* 把进程句柄保存为恢复令牌。 */
    uint64_t RecoveryCapabilities::encode(uint64_t handle) {
        if (!handle) {
            return 0;
        }

        const uint32_t slot = static_cast<uint32_t>(handle) - 1;
        uint64_t token = slot < capacity && cache_[slot].handle == handle
                             ? cache_[slot].token
                             : 0;
        const uint32_t tokenSlot = static_cast<uint32_t>(token) - 1;
        if (!token || tokenSlot >= capacity ||
            entries_[tokenSlot].token != token) {
            token = checkpoint_.put(handle);
            if (!token) {
                return 0;
            }

            if (slot < capacity) {
                cache_[slot] = {handle, token};
            }
        }

        const uint32_t index = static_cast<uint32_t>(token) - 1;
        if (index >= capacity) {
            return 0;
        }

        entries_[index] = {token, visit_};
        return token;
    }

    /* 通过恢复令牌重新取得句柄。 */
    uint64_t RecoveryCapabilities::decode(uint64_t token) {
        if (!token) {
            return 0;
        }

        const uint64_t handle = checkpoint_.get(token);
        if (!handle) {
            return 0;
        }

        const uint32_t index = static_cast<uint32_t>(token) - 1,
                       slot = static_cast<uint32_t>(handle) - 1;
        if (index >= capacity) {
            os::closeHandle(handle);
            return 0;
        }

        entries_[index] = {token, visit_};
        if (slot < capacity) {
            cache_[slot] = {handle, token};
        }

        return handle;
    }

    /* 回收本轮已停止使用的令牌。 */
    bool RecoveryCapabilities::collect() {
        for (Entry& entry : entries_) {
            if (entry.token && entry.visit != visit_) {
                if (!checkpoint_.drop(entry.token)) {
                    return false;
                }

                entry = {};
            }
        }

        return true;
    }

    /* 清理检查点中遗留的孤立令牌。 */
    bool RecoveryCapabilities::discardOrphans() {
        uint64_t cursor = 0;
        while (const uint64_t token = checkpoint_.next(cursor)) {
            const uint32_t slot = static_cast<uint32_t>(token) - 1;
            if ((slot >= capacity || entries_[slot].token != token) &&
                !checkpoint_.drop(token)) {
                return false;
            }
        }

        return true;
    }

    /* 识别重试请求并查找已有结果。 */
    int ReplayCache::begin(os::IpcMessage& message, Request& request,
                           os::Sys::Checkpoint& checkpoint) {
        if (message.length < 3 ||
            message.data[message.length - 3] != services::serviceReplayMagic) {
            return 0;
        }

        request.id = message.data[message.length - 2];
        request.resumeError =
            static_cast<int64_t>(message.data[message.length - 1]);
        message.length -= 3;
        if (!request.id ||
            (request.resumeError &&
             request.resumeError !=
                 static_cast<int>(os::ErrorCode::Interrupted) &&
             request.resumeError != static_cast<int>(os::ErrorCode::Timeout))) {
            return -1;
        }

        uint64_t generation = 0;
        const int status =
            checkpoint.queryGeneration(message.senderPid, generation);
        if (status) {
            return status;
        }

        const uint64_t hash = digest(message);
        uint32_t empty = noRecord;
        for (uint32_t i = 0; i < capacity; ++i) {
            Record& record = records[i];
            if (!record.task && empty == noRecord) {
                empty = i;
            }

            if (record.task != message.senderPid ||
                record.thread != message.senderTid ||
                record.generation != generation) {
                continue;
            }

            if (record.id == request.id) {
                if (record.digest != hash) {
                    return -1;
                }

                request.slot = i;
                return record.complete ? 1 : request.resumeError;
            }
        }

        if (request.resumeError) {
            return request.resumeError;
        }

        if (empty == noRecord) {
            return static_cast<int>(os::ErrorCode::OutOfMemory);
        }

        Record& record = records[empty];
        record = {};
        record.task = message.senderPid;
        record.thread = message.senderTid;
        record.generation = generation;
        record.id = request.id;
        record.digest = hash;
        request.slot = empty;
        return 0;
    }

    /* 保存请求回复及其资源令牌。 */
    bool ReplayCache::remember(uint32_t slot, const os::IpcMessage& reply,
                               RecoveryCapabilities& capabilities) {
        if (slot == noRecord) {
            return true;
        }

        if (slot >= capacity || reply.length > 65 || reply.handleCount > 2) {
            return false;
        }

        Record& record = records[slot];
        record.length = reply.length;
        record.handleCount = reply.handleCount;
        os::Runtime::copyBytes(record.words, reply.data,
                               reply.length * sizeof(uint64_t));
        for (uint32_t i = 0; i < reply.handleCount; ++i) {
            record.tokens[i] = capabilities.encode(reply.handles[i]);
            if (!record.tokens[i]) {
                return false;
            }

            record.rights[i] = reply.handleRights[i];
            record.options[i] = reply.handleOptions[i];
        }

        record.complete = true;
        return true;
    }

    /* 重建已保存的回复消息。 */
    bool ReplayCache::restoreReply(uint32_t slot,
                                   os::Sys::Checkpoint& checkpoint,
                                   os::IpcMessage& reply) const {
        if (slot >= capacity || !records[slot].complete) {
            return false;
        }

        const Record& record = records[slot];
        reply = {};
        reply.length = record.length;
        os::Runtime::copyBytes(reply.data, record.words,
                               record.length * sizeof(uint64_t));
        for (uint32_t i = 0; i < record.handleCount; ++i) {
            const uint64_t handle = checkpoint.get(record.tokens[i]);
            if (!handle) {
                for (uint32_t j = 0; j < reply.handleCount; ++j) {
                    os::closeHandle(reply.handles[j]);
                }

                reply = {};
                return false;
            }

            reply.handles[reply.handleCount++] = handle;
            reply.handleRights[i] = record.rights[i];
            reply.handleOptions[i] = record.options[i];
        }

        return true;
    }

    /* 处理客户端的结果接收确认。 */
    uint32_t ReplayCache::acknowledge(const os::IpcMessage& message,
                                      os::Sys::Checkpoint& checkpoint) {
        if (message.length != 2 || !message.data[0] || !message.data[1]) {
            return noRecord;
        }

        for (uint32_t i = 0; i < capacity; ++i) {
            Record& record = records[i];
            if (record.task == message.senderPid &&
                record.thread == message.data[1] &&
                record.id == message.data[0]) {
                uint64_t generation = 0;
                if (!checkpoint.queryGeneration(message.senderPid,
                                                generation) &&
                    record.generation == generation) {
                    return i;
                }
            }
        }

        return noRecord;
    }

    /* 保留缓存回复使用的资源。 */
    bool ReplayCache::retain(RecoveryCapabilities& capabilities) const {
        for (const Record& record : records) {
            for (uint32_t i = 0; i < record.handleCount; ++i) {
                if (!capabilities.keep(record.tokens[i])) {
                    return false;
                }
            }
        }

        return true;
    }

    /* 查询某个资源令牌是否仍被引用。 */
    bool ReplayCache::references(uint64_t token) const {
        for (const Record& record : records) {
            for (uint32_t i = 0; i < record.handleCount; ++i) {
                if (record.tokens[i] == token) {
                    return true;
                }
            }
        }

        return false;
    }

    /* 检查缓存记录的字段和边界。 */
    bool ReplayCache::valid() const {
        for (const Record& record : records) {
            if (record.task &&
                (!record.id || !record.thread || !record.generation ||
                 record.length > 65 || record.handleCount > 2 ||
                 (record.complete && !record.length))) {
                return false;
            }
        }

        return true;
    }

    /* 计算请求内容与随附句柄的摘要。 */
    uint64_t ReplayCache::digest(const os::IpcMessage& request) {
        uint64_t value =
            request.type ^ (static_cast<uint64_t>(request.length) << 32);
        for (uint32_t i = 0; i < request.length; ++i) {
            value = (value ^ request.data[i]) * 1099511628211ULL;
        }

        for (uint32_t i = 0; i < request.handleCount; ++i) {
            value =
                (value ^
                 os::Ipc::Handle(request.handles[i], false).queryIdentity()) *
                1099511628211ULL;
            value = (value ^ request.handleRights[i]) * 1099511628211ULL;
        }

        return value;
    }

}  // namespace services
