#include "service/common/records.h"
#include "kernel/ipc.h"
#include <os/device.h>

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "kernel/new.h"

namespace kernel {
namespace {
class Sys final : public EndpointService {
   public:
    int call(Task&, const IpcMessage& request, IpcMessage& reply) override {
        switch (static_cast<os::SystemOperation>(request.type)) {
            case os::SystemOperation::MonotonicTime: reply.data[0] = time::getMonotonicTime(); break;
            case os::SystemOperation::WallClockTime: reply.data[0] = time::getWallClockTime(); break;
            case os::SystemOperation::ClockResolution: reply.data[0] = time::getClockResolution(); break;
            case os::SystemOperation::Info: {
                const os::wire::SystemInfo info{time::getMonotonicTime(), memory::getTotalMemoryBytes(),
                    memory::getFreeMemoryBytes(), smp::getOnlineCpuCount(), 0};
                MessageData::copy(&reply.data[1], &info, sizeof(info));
                break;
            }
            case os::SystemOperation::Log: {
                const char* text;
                if (!MessageData::name(request, 0, text, 512)) return -1;
                serial::serialWrite(text);
                break;
            }
            default: return -1;
        }
        reply.length = os::ipcDataWordCount;
        return 0;
    }
};



}  // namespace
EndpointService& systemKernelService() {
    static Sys service;
    return service;
}
}  // namespace kernel
