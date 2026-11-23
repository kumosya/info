/* 服务检查点、恢复令牌和已完成请求的保存。 */

#pragma once
#include <os/runtime.h>

#include "service/common/protocol.h"

namespace services {

    // Local handles are a cache; only protected tokens are serialized.
    /* 把运行时句柄与恢复令牌互相转换，使重启后的实例可以重新领取对象。 */
    class RecoveryCapabilities {
        public:
            explicit RecoveryCapabilities(os::Sys::Checkpoint& checkpoint)
                : checkpoint_(checkpoint) {
            }

            void begin() {
                ++visit_;
            }

            bool keep(uint64_t token);
            uint64_t encode(uint64_t handle);
            uint64_t decode(uint64_t token);
            bool collect();
            bool discardOrphans();

        private:
            static constexpr uint32_t capacity = 16384;

            /* 记录已代管对象的令牌与本实例句柄，避免重复领取。 */
            struct Cached {
                    uint64_t handle = 0, token = 0;
            } cache_[capacity];

            /* 保存所属表中的一个条目，由外层容器统一分配和回收。 */
            struct Entry {
                    uint64_t token = 0, visit = 0;
            } entries_[capacity];

            uint64_t visit_ = 1;
            os::Sys::Checkpoint& checkpoint_;
    };

    /* 保存已完成请求和回复，识别重试并防止有副作用的请求重复执行。 */
    class ReplayCache {
        public:
            static constexpr uint32_t capacity = 128, noRecord = capacity;

            /* 保存所属容器中的一条记录，其有效性由外层对象管理。 */
            struct Record {
                    uint64_t task = 0, thread = 0, generation = 0, id = 0,
                             digest = 0;
                    uint32_t length = 0, handleCount = 0;
                    uint64_t words[65]{}, tokens[2]{};
                    uint32_t rights[2]{}, options[2]{};
                    bool complete = false;
            };

            const Record& getRecord(uint32_t index) const {
                return records[index];
            }

            void setRecord(uint32_t index, const Record& record) {
                records[index] = record;
            }

            void clearRecord(uint32_t index) {
                records[index] = {};
            }

            /* 保存可重试请求的身份和内容摘要，用于匹配已完成结果。 */
            struct Request {
                    uint64_t id = 0;
                    int resumeError = 0;
                    uint32_t slot = noRecord;
            };

            // Signal handlers may issue nested RPCs on the same authenticated
            // thread.
            int begin(os::IpcMessage& message, Request& request,
                      os::Sys::Checkpoint& checkpoint);
            bool remember(uint32_t slot, const os::IpcMessage& reply,
                          RecoveryCapabilities& capabilities);
            bool restoreReply(uint32_t slot, os::Sys::Checkpoint& checkpoint,
                              os::IpcMessage& reply) const;
            uint32_t acknowledge(const os::IpcMessage& message,
                                 os::Sys::Checkpoint& checkpoint);
            bool retain(RecoveryCapabilities& capabilities) const;
            bool references(uint64_t token) const;
            bool valid() const;

        private:
            static uint64_t digest(const os::IpcMessage& request);
            Record records[capacity];
    };
}  // namespace services
