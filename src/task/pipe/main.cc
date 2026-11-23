/*
 * src/task/pipe/main.cc
 * 管道和双向通道服务，处理阻塞读写、端点引用及重启结果恢复。
 */

#include <fcntl.h>
#include <os/detail/client.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>
#include <poll.h>

#include "service/common/protocol.h"
#include "service/common/recovery.h"

class PipeServer {
        static constexpr size_t capacity = 64, pendingCapacity = 128,
                                bufferSize = 16384;

        struct Stream {
                unsigned char bytes[bufferSize]{};
                uint32_t head = 0, count = 0, eof = 0;
        };

        struct End {
                uint64_t handle = 0, id = 0, token = 0;
                bool nonblocking = false;
        };

        struct Pair {
                bool used = false, channel = false;
                End ends[2];
                Stream streams[2];
        } pairs_[capacity];

        // 等待记录保存客户端令牌和操作参数，原请求函数返回后仍能继续处理阻塞读写。
        struct Pending {
                bool used = false;
                uint32_t pair = 0, end = 0, size = 0;
                services::PipeOperation operation{};
                uint64_t token = 0, handle = 0;
                uint32_t outcome = services::ReplayCache::noRecord;
                unsigned char data[512]{};
        } pending_[pendingCapacity];

        uint64_t endpoint_ = 0;
        os::Sys::Checkpoint checkpoint_;
        services::RecoveryCapabilities capabilities_{checkpoint_};
        services::ReplayCache results_;
        bool durableResults_[services::ReplayCache::capacity]{};

        struct Snapshot {
                uint64_t endpoint = 0;
                Pair pairs[capacity];
                services::ReplayCache results;
        } snapshot_;

        struct PairState {
                bool used = false, channel = false;
                End ends[2];

                struct Cursor {
                        uint32_t head = 0, count = 0, eof = 0;
                } streams[2];
        };

        // 增量记录只保存本次变化；恢复时先读取完整快照，再按提交顺序应用这些变化。
        struct Delta {
                uint32_t pair = capacity,
                         outcome = services::ReplayCache::noRecord;
                PairState value;
                services::ReplayCache::Record result;
                uint32_t stream = 2, offset = 0, size = 0;
                unsigned char bytes[512]{};
        } delta_;

        static_assert(sizeof(Delta) <= 4096 - 32);
        uint64_t nextCollection_ = 0;
        os::Mem::Share eventMemory_;
        os::Mem::Map eventMapping_;
        volatile uint32_t* eventSequence_ = nullptr;
        uint64_t watchedUntil_[capacity][2]{};
        bool watchedLive_[capacity][2]{};

        /*
         * 事件页只存变化序号。客户端拿只读映射，不能修改管道状态或伪造服务端通知。
         * 页按需创建并立即去掉名字；服务重启后客户端会取得新实例的页。
         */
        bool ensureEventPage() {
            if (eventSequence_) {
                return true;
            }

            char name[] = "/pipe-events-0000000000000000";
            uint64_t stamp = os::Sys().readMonotonicTime();
            const char digits[] = "0123456789abcdef";
            for (size_t index = 0; index < 16; ++index) {
                name[sizeof(name) - 2 - index] = digits[stamp & 15];
                stamp >>= 4;
            }

            if (eventMemory_.open(name, O_CREAT | O_EXCL | O_RDWR)) {
                return false;
            }

            const int unlinked = eventMemory_.unlink();
            if (unlinked || eventMemory_.resize(4096) ||
                eventMapping_.open(eventMemory_, 4096, 0, 3)) {
                eventMemory_.close();
                return false;
            }

            eventSequence_ = reinterpret_cast<volatile uint32_t*>(
                eventMapping_.getAddress());
            return true;
        }

        /* 先发布序号，再唤醒映射同一物理页的等待者，关闭“查完状态才入睡”的窗口。
         */
        void notifyReadiness() {
            os::notifyReadiness();
            if (eventSequence_) {
                __atomic_add_fetch(eventSequence_, 1, __ATOMIC_RELEASE);
                os::wakeAddress(eventSequence_, UINT32_MAX);
            }
        }

        /*
         * 就绪查询只观察缓冲区，不取走字节，也不进入读写请求的重试结果缓存。
         * 对端关闭由短期观察记录检测，避免为所有闲置管道反复查询内核引用数。
         */
        void queryReadiness(os::IpcMessage& request) {
            if (request.handleCount != 1) {
                reply(request.replyToken, -2);
                return;
            }

            const uint64_t identity =
                os::Ipc::Handle(request.handles[0], false).queryIdentity();
            for (size_t index = 0; index < capacity; ++index) {
                Pair& pair = pairs_[index];
                if (!pair.used) {
                    continue;
                }

                for (size_t side = 0; side < 2; ++side) {
                    if (!identity || pair.ends[side].id != identity) {
                        continue;
                    }

                    const bool peer = live(pair, 1 - side);
                    watchedUntil_[index][side] = UINT64_MAX;
                    watchedLive_[index][side] = peer;
                    const Stream& input = pair.streams[pair.channel ? side : 0];
                    const Stream& output =
                        pair.streams[pair.channel ? 1 - side : 0];
                    unsigned ready = 0;
                    if (request.handleRights[0] & 1) {
                        if (input.count || input.eof || !peer) {
                            ready |= POLLIN | POLLRDNORM;
                        }

                        if (!peer) {
                            ready |= POLLHUP;
                        }
                    }

                    if (request.handleRights[0] & 2) {
                        if (!peer) {
                            ready |= POLLERR;
                        } else if (output.count < bufferSize) {
                            ready |= POLLOUT | POLLWRNORM;
                        }
                    }

                    reply(request.replyToken, ready);
                    return;
                }
            }

            reply(request.replyToken, -2);
        }

        /*
         * 在恢复数据无法安全提交时结束服务，让监督者按恢复协议重新启动，避免继续使用未保存状态。
         */
        [[noreturn]] static void failed() {
            os::Sys().log("[PIPE] recovery transaction failed\n");
            os::exitTask(2);
        }

        /*
         * 把活管道端点保存到恢复能力表中，使服务重启后仍能取得相同通信对象。
         */
        bool retain() {
            capabilities_.begin();

            if (!capabilities_.encode(endpoint_)) {
                return false;
            }

            for (Pair& pair : pairs_) {
                if (pair.used) {
                    for (End& end : pair.ends) {
                        end.token = capabilities_.encode(end.handle);

                        if (!end.token) {
                            return false;
                        }
                    }
                }
            }

            return results_.retain(capabilities_);
        }

        /*
         * 将运行时管道记录转换成快照表示，保存恢复令牌而不是当前实例的临时句柄编号。
         */
        static void serialize(Pair& value, const Pair& pair) {
            if (!pair.used) {
                os::Runtime::clearMemory(&value, sizeof(value));

                return;
            }

            value = pair;

            for (End& end : value.ends) {
                end.handle = end.token;
            }
        }

        /*
         * 提交完整管道快照和重试结果，保证已确认给客户端的操作能够在重启后恢复。
         */
        bool save() {
            if (!retain()) {
                return false;
            }

            snapshot_.endpoint = capabilities_.encode(endpoint_);
            snapshot_.results = results_;

            for (uint32_t i = 0; i < capacity; ++i) {
                serialize(snapshot_.pairs[i], pairs_[i]);
            }

            if (checkpoint_.commit(&snapshot_)) {
                return false;
            }

            for (uint32_t i = 0; i < services::ReplayCache::capacity; ++i) {
                durableResults_[i] = results_.getRecord(i).task != 0;
            }

            return capabilities_.collect();
        }

        /*
         * 在处理修改前准备检查点，失败时停止继续修改运行状态。
         */
        bool prepare() {
            int status = checkpoint_.prepareLog(sizeof(delta_));

            // 增量日志空间不足时先保存完整快照，压缩已经包含的旧日志，再重新为本次变化预留空间。
            if (status == static_cast<int>(os::ErrorCode::WouldBlock)) {
                if (checkpoint_.prepare() || !save()) {
                    return false;
                }

                status = checkpoint_.prepareLog(sizeof(delta_));
            }

            return !status;
        }

        /*
         * 记录本次管道变化及请求结果，再提交恢复数据。
         * 只有保存成功之后才允许对客户端确认操作完成。
         */
        void commit(uint32_t pair, uint32_t outcome,
                    const Pending* request = nullptr) {
            if (!retain()) {
                failed();
            }

            // 每条增量从全零开始，只填本次实际变动，避免把上次日志的字节当新变化重放。
            os::Runtime::clearMemory(&delta_, sizeof(delta_));
            delta_.pair = pair;
            delta_.outcome = outcome;

            if (pair < capacity && pairs_[pair].used) {
                const Pair& source = pairs_[pair];
                PairState& target = delta_.value;
                target.used = true;
                target.channel = source.channel;

                for (uint32_t i = 0; i < 2; ++i) {
                    target.ends[i] = source.ends[i];
                    target.ends[i].handle = target.ends[i].token;
                    target.streams[i] = {source.streams[i].head,
                                         source.streams[i].count,
                                         source.streams[i].eof};
                }

                // 写入请求除了保存缓冲区计数，还要记下新增字节及环形位置，重启后才能重建相同数据。
                if (request &&
                    request->operation == services::PipeOperation::Write) {
                    delta_.stream = source.channel ? 1 - request->end : 0;
                    const Stream& stream = source.streams[delta_.stream];
                    delta_.size = request->size;
                    delta_.offset =
                        (stream.head + stream.count - request->size) %
                        bufferSize;
                    os::Runtime::copyBytes(delta_.bytes, request->data,
                                           request->size);
                }
            }

            // 对应请求结果跟管道状态放进同一次提交，不能出现数据已消费而结果尚未保存的窗口。
            if (outcome < services::ReplayCache::capacity) {
                delta_.result = results_.getRecord(outcome);
            }

            if (checkpoint_.append(&delta_, sizeof(delta_))) {
                failed();
            }

            if (outcome < services::ReplayCache::capacity) {
                durableResults_[outcome] =
                    results_.getRecord(outcome).task != 0;
            }

            if (!capabilities_.collect()) {
                failed();
            }
        }

        /*
         * 读取并校验快照和增量记录，重新领取端点句柄，检查管道间关系和请求结果是否一致，再恢复可运行状态。
         */
        bool restore() {
            const int loaded = checkpoint_.open(&snapshot_, sizeof(snapshot_),
                                                0x5049504553543032ULL);

            if (loaded < 0) {
                return false;
            }

            // 首次启动没有可恢复快照，创建服务端点并提交初始空状态；恢复路径则继续重建旧对象。
            if (!loaded) {
                endpoint_ = os::detail::EndpointControl().create();

                return endpoint_ && !checkpoint_.prepare() && save();
            }

            // 按提交顺序应用增量，先恢复完整快照再补变化，不能倒序覆盖较新的状态。
            for (uint32_t i = 0; i < checkpoint_.getRecordCount(); ++i) {
                if (checkpoint_.readLog(i, &delta_, sizeof(delta_)) ||
                    delta_.pair > capacity ||
                    delta_.outcome > services::ReplayCache::capacity) {
                    return false;
                }

                if (delta_.size > 512 ||
                    (delta_.size &&
                     (delta_.pair >= capacity || !delta_.value.used ||
                      delta_.stream >= 2 || delta_.offset >= bufferSize))) {
                    return false;
                }

                if (delta_.pair < capacity) {
                    Pair& target = snapshot_.pairs[delta_.pair];
                    const PairState& source = delta_.value;

                    if (!source.used) {
                        os::Runtime::clearMemory(&target, sizeof(target));
                    } else {
                        target.used = true;
                        target.channel = source.channel;

                        for (uint32_t n = 0; n < 2; ++n) {
                            target.ends[n] = source.ends[n];
                            target.streams[n].head = source.streams[n].head;
                            target.streams[n].count = source.streams[n].count;
                            target.streams[n].eof = source.streams[n].eof;
                        }

                        if (delta_.size) {
                            for (uint32_t n = 0; n < delta_.size; ++n) {
                                target.streams[delta_.stream]
                                    .bytes[(delta_.offset + n) % bufferSize] =
                                    delta_.bytes[n];
                            }
                        }
                    }
                }

                if (delta_.outcome < services::ReplayCache::capacity) {
                    snapshot_.results.setRecord(delta_.outcome, delta_.result);
                }
            }

            // 增量应用结束后先校验结果表，损坏数据不能直接作为已完成操作回复给客户端。
            if (!snapshot_.results.valid()) {
                return false;
            }

            results_ = snapshot_.results;
            capabilities_.begin();

            // 从恢复令牌重新领取本实例句柄，旧服务保存的本地编号不能直接继续使用。
            endpoint_ = capabilities_.decode(snapshot_.endpoint);

            if (!endpoint_) {
                return false;
            }

            for (uint32_t i = 0; i < services::ReplayCache::capacity; ++i) {
                durableResults_[i] = results_.getRecord(i).task != 0;
            }

            // 未完成请求没有已提交副作用，由客户端重试，不能当作已完成结果恢复。
            for (uint32_t index = 0; index < services::ReplayCache::capacity;
                 ++index) {
                const services::ReplayCache::Record& record =
                    results_.getRecord(index);
                if (!record.complete) {
                    results_.clearRecord(index);
                }
            }

            for (uint32_t i = 0; i < capacity; ++i) {
                pairs_[i] = snapshot_.pairs[i];
                Pair& pair = pairs_[i];

                if (!pair.used) {
                    continue;
                }

                for (const Stream& stream : pair.streams) {
                    if (stream.head >= bufferSize ||
                        stream.count > bufferSize) {
                        return false;
                    }
                }

                for (End& end : pair.ends) {
                    if (!end.handle || end.handle != end.token) {
                        return false;
                    }

                    // 端点的恢复令牌稳定，本实例得到的句柄编号可能变化，恢复后重新核对对象仍有效。
                    end.handle = capabilities_.decode(end.token);

                    if (!end.handle ||
                        os::Ipc::Handle(end.handle, false).queryIdentity() !=
                            end.id) {
                        return false;
                    }
                }

                if (pair.ends[0].id == pair.ends[1].id) {
                    return false;
                }

                for (uint32_t n = 0; n < i; ++n) {
                    if (pairs_[n].used) {
                        for (const End& previous : pairs_[n].ends) {
                            for (const End& end : pair.ends) {
                                if (previous.id == end.id) {
                                    return false;
                                }
                            }
                        }
                    }
                }
            }

            for (uint32_t index = 0; index < services::ReplayCache::capacity;
                 ++index) {
                const services::ReplayCache::Record& record =
                    results_.getRecord(index);
                for (uint32_t n = 0; n < record.handleCount; ++n) {
                    bool found = false;

                    for (const Pair& pair : pairs_) {
                        if (pair.used) {
                            for (const End& end : pair.ends) {
                                if (end.token == record.tokens[n]) {
                                    found = true;
                                }
                            }
                        }
                    }

                    if (!found) {
                        return false;
                    }
                }
            }

            if (!results_.retain(capabilities_) ||
                !capabilities_.discardOrphans()) {
                return false;
            }

            os::Sys().log("[PIPE] byte streams and replay results restored\n");

            return true;
        }

        /*
         * 关闭非零端点句柄，用于释放管道端点和恢复代管对象。
         */
        static void closeTag(uint64_t handle) {
            if (handle) {
                os::Ipc::Handle(handle, false).destroyEndpoint();
                os::closeHandle(handle);
            }
        }

        /*
         * 释放指定管道槽的端点及相关等待状态，再清空槽位以供复用。
         */
        void release(size_t index) {
            Pair& pair = pairs_[index];

            if (!prepare()) {
                return;
            }

            pair.used = false;
            commit(index, services::ReplayCache::noRecord);

            for (End& end : pair.ends) {
                closeTag(end.handle);
            }

            pair = {};
        }

        /*
         * 检查管道某一端是否仍有客户端引用，据此判断读取结束和写入断管。
         */
        bool live(const Pair& pair, size_t end) {
            return os::Ipc::Handle(pair.ends[end].handle, false)
                       .queryReferences() > 1;
        }

        /*
         * 按令牌发送回复；只有真正成功回复才认为调用方已收到结果。
         */
        bool reply(uint64_t token, int64_t status,
                   os::IpcMessage* value = nullptr) {
            os::IpcMessage empty{};
            os::IpcMessage& result = value ? *value : empty;
            result.data[0] = status;

            if (!result.length) {
                result.length = 1;
            }

            return os::replyMessage(endpoint_, token, result) == 0;
        }

        /*
         * 完成一个等待请求，先保存可重试结果，再回复并清除等待记录，避免重启后重复消费数据。
         */

        // 先可靠保存结果，再通知客户端；服务重启后的重试才能得到相同回复。
        void finish(Pending& request, int64_t status,
                    os::IpcMessage* value = nullptr) {
            os::IpcMessage empty{};
            os::IpcMessage& result = value ? *value : empty;
            result.data[0] = status;

            if (!result.length) {
                result.length = 1;
            }

            // 先把本次返回值和必要引用记入重试缓存，客户端以后重发同一请求时不再重复读写。
            if (!results_.remember(request.outcome, result, capabilities_)) {
                failed();
            }

            // 管道变动和缓存结果都可靠提交后才回复客户端，保证成功回复有可恢复的依据。
            commit(request.pair, request.outcome, &request);
            notifyReadiness();

            os::replyMessage(endpoint_, request.token, result);
        }

        /*
         * 尝试推进一次读写。
         * 按普通管道或双向通道选择缓冲区，检查对端是否存活，数据不足或空间不足时继续等待。
         */
        bool attempt(Pending& request) {
            Pair& pair = pairs_[request.pair];
            const uint32_t end = request.end;
            End& self = pair.ends[end];

            if (request.operation == services::PipeOperation::Read) {
                Stream& stream = pair.streams[pair.channel ? end : 0];

                // 没数据但对端仍活着不代表读结束；阻塞模式继续等，非阻塞模式才立即报告暂时不可读。
                if (!stream.count && !stream.eof && live(pair, 1 - end)) {
                    if (!self.nonblocking) {
                        return false;
                    }

                    reply(request.token, -6);

                    return true;
                }

                // 本轮读取量取请求与已有数据中的较小值，不等待把整个请求长度填满。
                const uint32_t count =
                    stream.count < request.size ? stream.count : request.size;

                if (!prepare()) {
                    reply(request.token, -5);

                    return true;
                }

                os::IpcMessage result{};
                result.length = 1 + (count + 7) / 8;
                unsigned char* data =
                    reinterpret_cast<unsigned char*>(&result.data[1]);

                for (uint32_t i = 0; i < count; ++i) {
                    data[i] = stream.bytes[(stream.head + i) % bufferSize];
                }

                // 成功读出的字节从环形缓冲区消费，移动队首并减少数量，这些变动随后与回复结果一起提交。
                stream.head = (stream.head + count) % bufferSize;
                stream.count -= count;

                if (!count && stream.eof) {
                    --stream.eof;
                }

                finish(request, count, &result);

                return true;
            }

            Stream& stream = pair.streams[pair.channel ? 1 - end : 0];

            // 写入前确认接收端仍存活；对端已关闭时应报告断管，不能继续把字节塞进无人读取的缓冲区。
            if (!live(pair, 1 - end)) {
                reply(request.token, -13);

                return true;
            }

            // 该实现要求本次写入整体放得下，空间不足就等待或返回暂不可写，不进行半次写入。
            if (bufferSize - stream.count < request.size) {
                if (!self.nonblocking) {
                    return false;
                }

                reply(request.token, -6);

                return true;
            }

            if (!prepare()) {
                reply(request.token, -5);

                return true;
            }

            // 按队尾位置绕回写入请求字节，最后才增加有效数据量，使下一次读取看见完整新增部分。
            for (uint32_t i = 0; i < request.size; ++i) {
                stream.bytes[(stream.head + stream.count + i) % bufferSize] =
                    request.data[i];
            }

            stream.count += request.size;
            finish(request, request.size);

            return true;
        }

        /*
         * 循环推进可完成的等待请求，同时处理取消和过期调用，直到本轮没有状态变化。
         */
        void progress() {
            const uint64_t now = os::Sys().readMonotonicTime();
            for (size_t index = 0; index < capacity; ++index) {
                for (size_t side = 0; side < 2; ++side) {
                    if (pairs_[index].used &&
                        watchedUntil_[index][side] > now) {
                        const bool peer = live(pairs_[index], 1 - side);
                        if (peer != watchedLive_[index][side]) {
                            watchedLive_[index][side] = peer;
                            notifyReadiness();
                        }
                    }
                }
            }

            // 取消请求后先释放它的能力引用，再判断对应对象是否已经无人使用并回收。
            for (Pending& request : pending_) {
                if (request.used) {
                    if (os::Ipc::Handle(endpoint_, false)
                                .requestActive(request.token) != 1 ||
                        attempt(request)) {
                        os::closeHandle(request.handle);
                        request = {};
                    }
                }
            }

            // 两端客户端和重试结果都不再引用管道时，才释放服务自身保留的端点与槽位。
            for (size_t i = 0; i < capacity; ++i) {
                if (pairs_[i].used && !live(pairs_[i], 0) &&
                    !live(pairs_[i], 1) &&
                    !results_.references(pairs_[i].ends[0].token) &&
                    !results_.references(pairs_[i].ends[1].token)) {
                    release(i);
                }
            }

            if (os::Sys().readMonotonicTime() >= nextCollection_) {
                nextCollection_ = os::Sys().readMonotonicTime() + 1000000000ULL;

                for (uint32_t i = 0; i < services::ReplayCache::capacity; ++i) {
                    const services::ReplayCache::Record& record =
                        results_.getRecord(i);

                    if (!record.task) {
                        continue;
                    }

                    uint64_t generation = 0;
                    const int status =
                        checkpoint_.queryGeneration(record.task, generation);

                    // 客户端退出或执行新程序后，原映像的请求结果不再需要保留，可以提交一条删除记录释放缓存。
                    const bool dead =
                        status == static_cast<int>(os::ErrorCode::NotFound) ||
                        (!status && generation != record.generation);

                    if (dead && prepare()) {
                        results_.clearRecord(i);
                        commit(capacity, i);
                    }
                }
            }
        }

        /*
         * 创建普通管道或双向通道，准备两端句柄和恢复记录；保存成功才把句柄交给调用者。
         */
        void create(const os::IpcMessage& request, bool channel,
                    uint32_t outcome) {
            for (size_t i = 0; i < capacity; ++i) {
                if (!pairs_[i].used) {
                    if (!prepare()) {
                        reply(request.replyToken, -5);

                        return;
                    }

                    Pair& pair = pairs_[i];

                    // 先占槽并初始化两条流，再创建端点；失败要撤销本次资源，不能对外暴露只有一端的管道。
                    pair.used = true;
                    pair.channel = channel;

                    for (Stream& stream : pair.streams) {
                        stream.head = stream.count = stream.eof = 0;
                    }

                    for (End& end : pair.ends) {
                        end.handle = os::detail::EndpointControl().create();
                        end.id =
                            os::Ipc::Handle(end.handle, false).queryIdentity();
                        end.nonblocking = false;
                    }

                    if (!pair.ends[0].handle || !pair.ends[1].handle) {
                        for (End& end : pair.ends) {
                            closeTag(end.handle);
                        }

                        pair = {};
                        reply(request.replyToken, -5);

                        return;
                    }

                    os::IpcMessage result{};
                    result.handleCount = 2;
                    result.length = 1;

                    for (size_t n = 0; n < 2; ++n) {
                        result.handles[n] = pair.ends[n].handle;
                        result.handleOptions[n] =
                            services::ipcHandleRestrictRights;
                        result.handleRights[n] = channel ? 3 : n == 0 ? 1 : 2;
                    }

                    Pending transaction{};
                    transaction.pair = i;
                    transaction.outcome = outcome;
                    transaction.token = request.replyToken;
                    finish(transaction, 0, &result);

                    return;
                }
            }

            reply(request.replyToken, -5);
        }

        /*
         * 分发管道创建、读写、查询和关闭请求，使用请求编号识别重试，阻塞读写登记到等待表。
         */
        void handle(os::IpcMessage& request) {
            const services::PipeOperation operation =
                static_cast<services::PipeOperation>(request.type);

            if (operation == services::PipeOperation::Readiness) {
                queryReadiness(request);
                return;
            }

            if (operation == services::PipeOperation::EventPage) {
                if (!ensureEventPage()) {
                    reply(request.replyToken, -5);
                    return;
                }

                os::IpcMessage result{};
                result.handleCount = 1;
                result.handles[0] = eventMemory_.getHandle();
                result.handleRights[0] = 1;
                result.handleOptions[0] = 1;
                reply(request.replyToken, 0, &result);
                return;
            }

            // 客户端明确确认已收到结果后，才能提交删除重试缓存，避免服务重启后重复完成请求。
            if (operation == services::PipeOperation::Acknowledge) {
                const uint32_t slot =
                    results_.acknowledge(request, checkpoint_);

                if (slot != services::ReplayCache::noRecord) {
                    if (!durableResults_[slot] &&
                        !results_.getRecord(slot).complete) {
                        results_.clearRecord(slot);
                        reply(request.replyToken, 0);

                        return;
                    }

                    if (!prepare()) {
                        reply(request.replyToken, -5);

                        return;
                    }

                    results_.clearRecord(slot);
                    commit(capacity, slot);
                }

                reply(request.replyToken, 0);

                return;
            }

            // 先根据请求身份检查是否是重试，已完成请求重放结果，新的请求才进入读写与创建逻辑。
            services::ReplayCache::Request replay;
            const int resumed = results_.begin(request, replay, checkpoint_);

            // 已完成的重试只恢复旧回复和句柄，不再次消费管道字节或创建另一对端点。
            if (resumed == 1) {
                os::IpcMessage result{};

                if (!results_.restoreReply(replay.slot, checkpoint_, result)) {
                    failed();
                }

                os::replyMessage(endpoint_, request.replyToken, result);

                for (uint32_t i = 0; i < result.handleCount; ++i) {
                    os::closeHandle(result.handles[i]);
                }

                return;
            }

            if (resumed) {
                reply(request.replyToken, resumed);

                return;
            }

            if (request.length < 3) {
                reply(request.replyToken, -1);

                return;
            }

            // 新建类请求与已有端点操作分开，前者不应要求提供一个已经存在的管道句柄。
            if (operation == services::PipeOperation::Create ||
                operation == services::PipeOperation::CreateChannel) {
                create(request,
                       operation == services::PipeOperation::CreateChannel,
                       replay.slot);

                return;
            }

            if (request.handleCount != 1) {
                reply(request.replyToken, -2);

                return;
            }

            const uint64_t identity =
                os::Ipc::Handle(request.handles[0], false).queryIdentity();

            for (size_t index = 0; index < capacity; ++index) {
                if (pairs_[index].used) {
                    for (size_t side = 0; side < 2; ++side) {
                        Pair& pair = pairs_[index];

                        if (pair.ends[side].id != identity) {
                            continue;
                        }

                        if (operation == services::PipeOperation::Identity) {
                            reply(request.replyToken,
                                  pair.channel ? identity : 0);

                            return;
                        }

                        // 非阻塞标志属于管道端，修改也要写入恢复记录，否则服务重启后客户端语义会改变。
                        if (operation == services::PipeOperation::Nonblocking) {
                            if (request.data[1] > 1) {
                                reply(request.replyToken, -1);

                                return;
                            }

                            if (!prepare()) {
                                reply(request.replyToken, -5);

                                return;
                            }

                            pair.ends[side].nonblocking = request.data[1];
                            Pending transaction{};
                            transaction.pair = index;
                            transaction.outcome = replay.slot;
                            transaction.token = request.replyToken;
                            finish(transaction, 0);

                            return;
                        }

                        if (operation == services::PipeOperation::Eof) {
                            if (!pair.channel ||
                                !(request.handleRights[0] & 2)) {
                                reply(request.replyToken, -3);

                                return;
                            }

                            if (pair.streams[1 - side].eof == UINT32_MAX ||
                                !prepare()) {
                                reply(request.replyToken, -5);

                                return;
                            }

                            ++pair.streams[1 - side].eof;
                            Pending transaction{};
                            transaction.pair = index;
                            transaction.outcome = replay.slot;
                            transaction.token = request.replyToken;
                            finish(transaction, 0);

                            return;
                        }

                        if ((operation != services::PipeOperation::Read &&
                             operation != services::PipeOperation::Write) ||
                            !request.data[1] || request.data[1] > 512) {
                            reply(request.replyToken, -1);

                            return;
                        }

                        // 正式读写前根据操作方向检查端点权限，不能用只读句柄发起写入。
                        const bool read =
                            operation == services::PipeOperation::Read;

                        if (!(request.handleRights[0] & (read ? 1 : 2)) ||
                            (!read &&
                             request.data[1] > (request.length - 3) * 8)) {
                            reply(request.replyToken, -3);

                            return;
                        }

                        Pending pending{};
                        pending.used = true;
                        pending.pair = index;
                        pending.end = side;
                        pending.operation = operation;
                        pending.size = request.data[1];
                        pending.token = request.replyToken;
                        pending.outcome = replay.slot;

                        if (!read) {
                            os::Runtime::copyBytes(
                                pending.data, &request.data[3], pending.size);
                        }

                        if (attempt(pending)) {
                            return;
                        }

                        // 暂时无法完成的阻塞请求放进等待表，保留必要端点引用，之后由
                        // progress 再尝试推进。
                        for (Pending& slot : pending_) {
                            if (!slot.used) {
                                pending.handle = request.handles[0];
                                slot = pending;
                                request.handleCount = 0;

                                return;
                            }
                        }

                        reply(request.replyToken, -5);

                        return;
                    }
                }
            }

            reply(request.replyToken, -2);
        }

    public:
        /*
         * 恢复或创建管道服务端点，注册名称后进入请求循环，并持续推进等待读写。
         */
        int run() {
            if (!restore()) {
                return 1;
            }

            if (!os::detail::RegistryClient(os::getBootstrapHandle())
                     .registerName("pipe", endpoint_)) {
                return 1;
            }

            os::notifyReadiness();
            os::Sys().log("[PIPE] Ring 3 service ready\n");

            while (true) {
                progress();
                os::IpcMessage request{};

                if (os::receiveMessage(
                        endpoint_, request,
                        os::Sys().readMonotonicTime() + 10000000) == 0) {
                    handle(request);

                    for (uint32_t n = 0; n < request.handleCount; ++n) {
                        os::closeHandle(request.handles[n]);
                    }

                    progress();
                }
            }
        }
};

/*
 * 创建管道服务对象并运行主循环。
 */
int main(int, char**) {
    static PipeServer server;

    return server.run();
}
