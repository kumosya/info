#include "service/common/kernel-requests.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace os {
int Sys::Recovery::acquire(IpcMessage& snapshot) {
    IpcMessage request{};
    request.type     = static_cast<uint64_t>(TaskOperation::RecoveryAcquire);
    request.length   = 5;
    const int status = result(request, snapshot);
    if (!status) epoch_ = snapshot.data[1];
    return status;
}

int Sys::Recovery::invoke(uint64_t operation, uint64_t a, uint64_t b,
                          uint64_t c, IpcMessage& reply) {
    if (!epoch_) return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{};
    request.type    = operation;
    request.length  = 5;
    request.data[0] = epoch_;
    request.data[1] = a;
    request.data[2] = b;
    request.data[3] = c;
    return result(request, reply);
}

int Sys::Recovery::prepare(uint64_t bytes, bool logRecord) {
    IpcMessage reply{};
    return invoke(static_cast<uint64_t>(TaskOperation::RecoveryPrepare), bytes,
                  logRecord, 0, reply);
}

int Sys::Recovery::commit(uint64_t handle, uint64_t bytes, uint64_t sequence,
                          IpcMessage& reply) {
    return invoke(static_cast<uint64_t>(TaskOperation::RecoveryCommit), handle,
                  bytes, sequence, reply);
}

int Sys::Recovery::append(uint64_t handle, uint64_t bytes, uint64_t sequence,
                          IpcMessage& reply) {
    return invoke(static_cast<uint64_t>(TaskOperation::RecoveryAppend), handle,
                  bytes, sequence, reply);
}

int Sys::Recovery::readLog(uint32_t index, IpcMessage& reply) {
    return invoke(static_cast<uint64_t>(TaskOperation::RecoveryReadLog), index,
                  0, 0, reply);
}

uint64_t Sys::Recovery::getEpoch() const { return epoch_; }
}  // namespace os
