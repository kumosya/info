/* 验证父设备顺序、队列背压、请求合并及部分完成的分配。 */
#include <cassert>

#include "service/device/storage/block_backend.h"
#include "service/device/storage/request_queue.h"

/* 串行设备的测试替身，保留每条命令各自的完成前缀。 */
class SerialDisk final : public storage::BlockBackend {
    public:
        storage::BlockInfo info() const override {
            return {1024, 512, 8, true, false, storage::CacheMode::WriteThrough,
                    false};
        }

        storage::TransferResult read(uint64_t, uint32_t count, void*,
                                     size_t) override {
            ++calls_;
            return {os::Storage::Error::Success, count};
        }

        storage::TransferResult write(uint64_t, uint32_t, const void*,
                                      size_t) override {
            ++calls_;
            return {os::Storage::Error::Io, 1};
        }

        os::Storage::Error flush() override {
            return os::Storage::Error::Success;
        }

        storage::StopResult stop() override {
            return storage::StopResult::Stopped;
        }

        uint32_t getCalls() const {
            return calls_;
        }

    private:
        uint32_t calls_ = 0;
};

int main() {
    // 串行批次仍逐条执行，各请求保留自己的错误与完成进度。
    SerialDisk disk;
    uint8_t buffer[4096];
    storage::BlockTransfer transfers[] = {
        {0, 2, buffer, sizeof(buffer), false},
        {20, 3, buffer, sizeof(buffer), true},
        {50, 1, buffer, sizeof(buffer), false}};
    disk.submitBatch(transfers, 3);
    assert(disk.getCalls() == 3);
    assert(transfers[0].getResult().getCompleted() == 2);
    assert(transfers[1].getResult().getError() == os::Storage::Error::Io &&
           transfers[1].getResult().getCompleted() == 1);
    assert(transfers[2].getResult().getError() == os::Storage::Error::Success);

    using Queue = storage::RequestQueue;
    using Direction = storage::QueueDirection;
    storage::QueuePolicy policies[2] = {{storage::QueueMode::Parallel, 7},
                                        {storage::QueueMode::Serial, 128}};
    assert(policies[0].getDepth() == 7 && policies[1].getDepth() == 1);
    assert(policies[1].getBlockLimit() == 8);
    Queue queue;
    uint32_t indices[Queue::batchCapacity];

    // 相邻写可以同批；刷新必须等待先前整批完成，后续读也不能越过刷新。
    const uint32_t first = queue.enqueue(0, Direction::Write, 10, 2);
    const uint32_t second = queue.enqueue(0, Direction::Write, 12, 3);
    const uint32_t flush = queue.enqueue(0, Direction::Barrier, 0, 0);
    const uint32_t read = queue.enqueue(0, Direction::Read, 10, 5);
    const uint32_t other = queue.enqueue(1, Direction::Read, 10, 1);
    assert(queue.take(policies, 2, indices) == 2);
    assert(indices[0] == first && indices[1] == second);
    uint32_t independent[Queue::batchCapacity];
    assert(queue.take(policies, 2, independent) == 1 &&
           independent[0] == other);
    queue.finish(independent, 1);
    assert(queue.take(policies, 2, independent) == 0);
    queue.finish(indices, 2);
    assert(queue.take(policies, 2, indices) == 1 && indices[0] == flush);
    queue.finish(indices, 1);
    assert(queue.take(policies, 2, indices) == 1 && indices[0] == read);
    queue.finish(indices, 1);

    // 重叠写保持先后执行，防止设备并行执行时改变最终内容。
    queue.enqueue(0, Direction::Write, 20, 4);
    queue.enqueue(0, Direction::Write, 22, 4);
    assert(queue.take(policies, 2, indices) == 1);
    queue.finish(indices, 1);
    assert(queue.take(policies, 2, indices) == 1);
    queue.finish(indices, 1);
    assert(queue.enqueue(0, Direction::Read, UINT64_MAX, 1) == Queue::capacity);
    for (uint32_t index = 0; index < Queue::capacity; ++index) {
        assert(queue.enqueue(0, Direction::Read, index, 1) != Queue::capacity);
    }

    assert(queue.enqueue(0, Direction::Read, 0, 1) == Queue::capacity);

    // 五个块合成一条命令；后续超过上限或不连续的范围分成新命令。
    storage::MergePlan plan(7);
    assert(plan.append(10, 2) && plan.append(12, 3));
    assert(plan.getGroupCount() == 1 && plan.getBlocks(0) == 5);
    assert(plan.getOffset(1) == 2);
    assert(plan.completed(0, 3) == 2 && plan.completed(1, 3) == 1);
    assert(plan.completed(1, 0) == 0 && plan.completed(1, 5) == 3);
    assert(plan.append(15, 3) && plan.getGroupCount() == 2);
    assert(plan.append(100, 1) && plan.getGroupCount() == 3);
    assert(!plan.append(0, 8) && !plan.append(UINT64_MAX, 1));
}
