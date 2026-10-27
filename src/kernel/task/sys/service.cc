/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sys/service.cc
 * System time, memory and CPU information, and bounded serial logging.
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/records.h"
#include <os/device.h>

namespace kernel::task {

namespace {
namespace Sys {
// Resident information requests need no persistent caller state. Time values
// are nanoseconds; Info combines independently sampled counters and is not a
// frozen global snapshot. Unknown types fail rather than fall through.
int call(Task&, const IpcMessage& request, IpcMessage& reply) {
    switch (static_cast<os::SystemOperation>(request.type)) {
        case os::SystemOperation::MonotonicTime:
            reply.data[0] = time::getMonotonicTime();
            break;
        case os::SystemOperation::WallClockTime:
            reply.data[0] = time::getWallClockTime();
            break;
        case os::SystemOperation::ClockResolution:
            reply.data[0] = time::getClockResolution();
            break;
        case os::SystemOperation::Info: {
            const os::wire::SystemInfo info{
                time::getMonotonicTime(), mm::getTotalMemoryBytes(),
                mm::getFreeMemoryBytes(), cpu::getOnlineCpuCount(), 0};
            copyMessageData(&reply.data[1], &info, sizeof(info));
            break;
        }
        // The string is embedded in message words, not an arbitrary caller
        // pointer. Bound its terminator by both message length and the
        // 512-byte log limit before passing it to the serial implementation.
        case os::SystemOperation::Log: {
            const char* text;
            if (!readMessageName(request, 0, text, 512)) return -1;
            device::serialWrite(text);
            break;
        }
        default:
            return -1;
    }
    reply.length = os::ipcDataWordCount;
    return 0;
}
}  // namespace Sys

}  // namespace
// Bind concrete information/logging dispatch. Clock implementation and
// hardware counters live in their own modules, not in this service object.
EndpointService& systemKernelService() {
    static EndpointService service(Sys::call);
    return service;
}

}  // namespace kernel::task
