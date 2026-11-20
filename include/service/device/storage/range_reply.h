#pragma once

#include "service/device/storage/block_backend.h"

namespace storage {
constexpr os::Storage::Error storageError(uint64_t word) {
    using Error      = os::Storage::Error;
    const auto value = static_cast<int64_t>(word);
    return value <= 0 && value >= static_cast<int64_t>(Error::CapabilityUnknown)
               ? static_cast<Error>(value)
               : Error::Io;
}

constexpr os::Storage::Error transportError(int64_t status) {
    using Error = os::Storage::Error;
    if (status == static_cast<int64_t>(os::ErrorCode::Timeout))
        return Error::Timeout;
    if (status == static_cast<int64_t>(os::ErrorCode::EndpointClosed) ||
        status == static_cast<int64_t>(os::ErrorCode::PeerDied))
        return Error::DeviceOffline;
    return Error::Io;
}

// Keep the status word and byte count used by existing range replies.
inline void encodeRangeReply(os::IpcMessage& reply, TransferResult result,
                             uint32_t requested, uint32_t blockSize,
                             bool read) {
    using Error = os::Storage::Error;
    if (result.getCompleted() > requested ||
        (result.getError() == Error::Success &&
         result.getCompleted() != requested))
        result = {Error::Io, 0};
    reply.data[0] = static_cast<uint64_t>(result.getError());
    reply.length  = 1;
    if (!result.getCompleted()) return;
    const uint32_t bytes = result.getCompleted() * blockSize;
    reply.data[1]        = bytes;
    reply.length         = read ? 2 + bytes / sizeof(uint64_t) : 2;
}

inline TransferResult decodeRangeReply(const os::IpcMessage& reply,
                                       uint32_t requested, uint32_t blockSize,
                                       bool read) {
    using Error = os::Storage::Error;
    if (!blockSize || blockSize % sizeof(uint64_t) || reply.length < 1 ||
        reply.length > os::ipcDataWordCount)
        return {Error::Io, 0};
    const auto error     = storageError(reply.data[0]);
    const uint64_t bytes = reply.length >= 2 ? reply.data[1] : 0;
    if (bytes % blockSize || bytes / blockSize > requested ||
        (error == Error::Success && bytes / blockSize != requested) ||
        (read && bytes && reply.length < 2 + bytes / sizeof(uint64_t)))
        return {Error::Io, 0};
    return {error, static_cast<uint32_t>(bytes / blockSize)};
}

template <typename Operation>
TransferResult transferBatches(uint32_t count, uint32_t limit,
                               Operation operation) {
    using Error = os::Storage::Error;
    if (!limit) return {Error::InvalidArgument, 0};
    uint32_t done = 0;
    while (done < count) {
        const uint32_t batch = count - done < limit ? count - done : limit;
        const auto result    = operation(done, batch);
        if (result.getCompleted() > batch ||
            (result.getError() == Error::Success &&
             result.getCompleted() != batch))
            return {Error::Io, done};
        done += result.getCompleted();
        if (result.getError() != Error::Success)
            return {result.getError(), done};
    }
    return {Error::Success, done};
}
}  // namespace storage
