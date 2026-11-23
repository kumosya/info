/* 存储协议边界和异常完成回归，不依赖真实设备。 */
#include <cassert>
#include "service/common/protocol.h"
#include "service/device/storage/range_reply.h"
#include "service/device/storage/request_lifecycle.h"

int main()
{
    using Error = os::Storage::Error;
    using Mode = storage::CacheMode;
    using Action = storage::FlushAction;
    static_assert(static_cast<uint64_t>(services::StorageRequest::WriteRange) == 0x108);
    static_assert(static_cast<uint64_t>(services::StorageRequest::GetStatistics) == 0x109);

    /* 只接受提交过且尚未完成的描述符，拒绝越界、重复和停机后的完成项。 */
    storage::VirtioRequestSlots<7> slots;
    assert(!slots.recordUsed(0, 513));
    slots.submit(0);
    assert(!slots.recordUsed(1, 513));
    assert(!slots.recordUsed(21, 513));
    assert(slots.recordUsed(0, 513));
    assert(slots.completed(0) && slots.usedLength(0) == 513);
    assert(!slots.recordUsed(0, 513));
    slots.consume(0);
    assert(slots.findFree() == 0);
    slots.submit(0);
    slots.freeze();
    assert(slots.findFree() == 7 && !slots.recordUsed(0, 513));
    assert(slots.validUsedAdvance(65535, 1));
    assert(!slots.validUsedAdvance(0, 8));
    assert(storage::expired(UINT64_MAX - 5, 4, 10));

    /* 写回缓存不能假装已落盘；只有明确直写或真实 FLUSH 才能报告成功。 */
    assert((storage::BlockInfo{1024, 512, 7, true, false, Mode::WriteThrough, false}).getFlushAction() == Action::Complete);
    assert((storage::BlockInfo{1024, 512, 7, true, false, Mode::WriteBack, false}).getFlushAction() == Action::Unsupported);
    assert((storage::BlockInfo{1024, 512, 7, true, false, Mode::WriteBack, true}).getFlushAction() == Action::Command);
    assert((storage::BlockInfo{1024, 512, 7, true, false, Mode::Unknown, false}).getFlushAction() == Action::Unknown);
    uint64_t physical = 0;
    assert(storage::translatePartitionRange(1000, 100, 500, 499, 1, physical) && physical == 599);
    assert(!storage::translatePartitionRange(1000, 100, 500, 499, 2, physical));
    assert(!storage::translatePartitionRange(UINT64_MAX, UINT64_MAX - 1, 2, 0, 1, physical));

    /* 分批请求失败只能确认已完成的连续前缀，不能把未写出的后半段计入成功。 */
    const storage::TransferResult partial = storage::transferBatches(20, 7,
        [](uint32_t done, uint32_t batch) -> storage::TransferResult
        {
            return done == 7 ? storage::TransferResult{Error::Io, 2}
                             : storage::TransferResult{Error::Success, batch};
        });
    assert(partial.getError() == Error::Io && partial.getCompleted() == 9);
    os::IpcMessage reply{};
    storage::encodeRangeReply(reply, {Error::Io, 2}, 7, 512, true);
    const storage::TransferResult decoded = storage::decodeRangeReply(reply, 7, 512, true);
    assert(decoded.getError() == Error::Io && decoded.getCompleted() == 2);
    reply.data[1] = 511;
    assert(storage::decodeRangeReply(reply, 20, 512, true).getCompleted() == 0);
    reply.data[0] = 0;
    reply.data[1] = 512;
    assert(storage::decodeRangeReply(reply, 2, 512, false).getError() == Error::Io);
    return 0;
}
