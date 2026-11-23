#include "service/device/storage/request_queue.h"

namespace storage {

    /* 合并范围的上限来自驱动能力和工作缓冲区大小。 */
    MergePlan::MergePlan(uint32_t limit) : limit_(limit) {
    }

    /* 只合并接收顺序相邻且磁盘范围连续的请求。 */
    bool MergePlan::append(uint64_t first, uint32_t blocks) {
        if (members_ == RequestQueue::batchCapacity || !blocks ||
            blocks > limit_ || first > UINT64_MAX - blocks) {
            return false;
        }

        if (!groups_ || first_[groups_ - 1] + blocks_[groups_ - 1] != first ||
            blocks > limit_ - blocks_[groups_ - 1]) {
            first_[groups_++] = first;
        }

        const uint32_t group = groups_ - 1;
        group_[members_] = group;
        offset_[members_] = blocks_[group];
        size_[members_++] = blocks;
        blocks_[group] += blocks;
        return true;
    }

    /* 读取合并后的命令范围和原请求在命令中的位置。 */
    uint32_t MergePlan::getGroupCount() const {
        return groups_;
    }

    uint32_t MergePlan::getGroup(uint32_t member) const {
        return group_[member];
    }

    uint32_t MergePlan::getOffset(uint32_t member) const {
        return offset_[member];
    }

    uint64_t MergePlan::getFirst(uint32_t group) const {
        return first_[group];
    }

    uint32_t MergePlan::getBlocks(uint32_t group) const {
        return blocks_[group];
    }

    /* 前半段成功、后半段失败时，每条请求只报告自己的已完成部分。 */
    uint32_t MergePlan::completed(uint32_t member, uint32_t prefix) const {
        const uint32_t available =
            prefix > offset_[member] ? prefix - offset_[member] : 0;
        return available < size_[member] ? available : size_[member];
    }

    /* IDE/AHCI 逐条发命令；VirtIO 可同时发布多个独立命令。 */
    QueuePolicy::QueuePolicy(QueueMode mode, uint32_t blocks)
        : mode_(mode), blocks_(blocks > 8 ? 8 : blocks) {
    }

    /* 限制一次取出的请求数量，给其他设备和接收线程留下执行机会。 */
    uint32_t QueuePolicy::getBatchSize() const {
        return mode_ == QueueMode::Memory ? 4 : RequestQueue::batchCapacity;
    }

    /* 并行深度与驱动提供的硬件请求槽相匹配。 */
    uint32_t QueuePolicy::getDepth() const {
        return mode_ == QueueMode::Parallel ? RequestQueue::batchCapacity : 1;
    }

    /* 合并后的数据必须装得进工作缓冲区和驱动 DMA 缓冲区。 */
    uint32_t QueuePolicy::getBlockLimit() const {
        return blocks_;
    }

    /* 保存一条队列记录的顺序、范围和当前处理状态。 */
    RequestQueue::Entry::Entry(State stateValue, QueueDirection directionValue,
                               uint32_t deviceValue, uint32_t blocksValue,
                               uint64_t firstValue, uint64_t sequenceValue)
        : state(stateValue),
          direction(directionValue),
          device(deviceValue),
          blocks(blocksValue),
          first(firstValue),
          sequence(sequenceValue) {
    }

    /* 序号表示服务收到请求的顺序，分区共用其父设备的队列。 */
    uint32_t RequestQueue::enqueue(uint32_t device, QueueDirection direction,
                                   uint64_t first, uint32_t blocks) {
        if (device >= maximumDevices ||
            (direction != QueueDirection::Barrier &&
             (!blocks || first > UINT64_MAX - blocks))) {
            return capacity;
        }

        for (uint32_t index = 0; index < capacity; ++index) {
            if (entries_[index].state == State::Free) {
                entries_[index] = Entry(State::Pending, direction, device,
                                        blocks, first, nextSequence_++);
                return index;
            }
        }

        return capacity;
    }

    /* 找到该物理设备最早收到的待处理请求。 */
    uint32_t RequestQueue::oldest(uint32_t device) const {
        uint32_t found = capacity;
        for (uint32_t index = 0; index < capacity; ++index) {
            if (entries_[index].state == State::Pending &&
                entries_[index].device == device &&
                (found == capacity ||
                 entries_[index].sequence < entries_[found].sequence)) {
                found = index;
            }
        }

        return found;
    }

    /* 同一设备一次只交给一个工作线程，刷新请求单独执行。 */
    uint32_t RequestQueue::take(const QueuePolicy* policies, uint32_t devices,
                                uint32_t* indices) {
        if (!policies || !indices || devices > maximumDevices) {
            return 0;
        }

        uint32_t first = capacity;
        for (uint32_t device = 0; device < devices; ++device) {
            const uint32_t candidate =
                active_[device] ? capacity : oldest(device);
            if (candidate != capacity &&
                (first == capacity ||
                 entries_[candidate].sequence < entries_[first].sequence)) {
                first = candidate;
            }
        }

        if (first == capacity) {
            return 0;
        }

        const uint32_t device = entries_[first].device;
        const QueueDirection direction = entries_[first].direction;
        uint32_t count = 0;
        while (count < policies[device].getBatchSize()) {
            const uint32_t next = oldest(device);
            if (next == capacity || entries_[next].direction != direction ||
                (count && direction == QueueDirection::Barrier)) {
                break;
            }

            // 重叠写按接收顺序执行，避免两个在途命令覆盖同一范围。
            bool overlaps = false;
            for (uint32_t prior = 0;
                 prior < count && direction == QueueDirection::Write; ++prior) {
                const Entry& left = entries_[indices[prior]];
                const Entry& right = entries_[next];
                overlaps |= left.first < right.first + right.blocks &&
                            right.first < left.first + left.blocks;
            }

            if (overlaps) {
                break;
            }

            entries_[next].state = State::Active;
            indices[count++] = next;
        }

        active_[device] = true;
        return count;
    }

    /* 完成通知发出后释放槽位，再允许下一批越过该设备的顺序边界。 */
    void RequestQueue::finish(const uint32_t* indices, uint32_t count) {
        for (uint32_t index = 0; index < count; ++index) {
            if (indices[index] < capacity &&
                entries_[indices[index]].state == State::Active) {
                active_[entries_[indices[index]].device] = false;
                entries_[indices[index]].state = State::Free;
            }
        }
    }

}  // namespace storage
