/* 块请求的排队顺序与设备调度策略。 */
#pragma once
#include <cstdint>

namespace storage {

    enum class QueueDirection : uint8_t { Read, Write, Barrier };
    enum class QueueMode : uint8_t { Memory, Serial, Parallel };

    /* 保存设备允许的批次大小、单次传输量和并行提交数量。 */
    class QueuePolicy {
        public:
            QueuePolicy() = default;
            QueuePolicy(QueueMode mode, uint32_t blocks);
            uint32_t getBatchSize() const;
            uint32_t getDepth() const;
            uint32_t getBlockLimit() const;

        private:
            QueueMode mode_ = QueueMode::Serial;
            uint32_t blocks_ = 1;
    };

    /* 保存等待处理的请求；调用者在修改队列时持有同一把锁。 */
    class RequestQueue {
        public:
            static constexpr uint32_t capacity = 64;
            static constexpr uint32_t maximumDevices = 8;
            static constexpr uint32_t batchCapacity = 7;

            RequestQueue() = default;
            uint32_t enqueue(uint32_t device, QueueDirection direction,
                             uint64_t first, uint32_t blocks);
            uint32_t take(const QueuePolicy* policies, uint32_t devices,
                          uint32_t* indices);
            void finish(const uint32_t* indices, uint32_t count);

        private:
            enum class State : uint8_t { Free, Pending, Active };

            class Entry {
                private:
                    friend class RequestQueue;
                    Entry() = default;
                    Entry(State state, QueueDirection direction,
                          uint32_t device, uint32_t blocks, uint64_t first,
                          uint64_t sequence);
                    State state = State::Free;
                    QueueDirection direction = QueueDirection::Barrier;
                    uint32_t device = 0, blocks = 0;
                    uint64_t first = 0, sequence = 0;
            };

            uint32_t oldest(uint32_t device) const;
            Entry entries_[capacity]{};
            bool active_[maximumDevices]{};
            uint64_t nextSequence_ = 0;
    };

    /* 记录相邻请求如何合成命令，以及完成字节属于哪个原请求。 */
    class MergePlan {
        public:
            explicit MergePlan(uint32_t limit);
            bool append(uint64_t first, uint32_t blocks);
            uint32_t getGroupCount() const;
            uint32_t getGroup(uint32_t member) const;
            uint32_t getOffset(uint32_t member) const;
            uint64_t getFirst(uint32_t group) const;
            uint32_t getBlocks(uint32_t group) const;
            uint32_t completed(uint32_t member, uint32_t prefix) const;

        private:
            uint32_t limit_, members_ = 0, groups_ = 0;
            uint64_t first_[RequestQueue::batchCapacity]{};
            uint32_t blocks_[RequestQueue::batchCapacity]{};
            uint32_t group_[RequestQueue::batchCapacity]{};
            uint32_t offset_[RequestQueue::batchCapacity]{};
            uint32_t size_[RequestQueue::batchCapacity]{};
    };

}  // namespace storage
