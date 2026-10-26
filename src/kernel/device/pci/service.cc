#include "service/common/records.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
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
class PciService final : public EndpointService {
 public:
    int call(Task& caller, const IpcMessage& request, IpcMessage& reply) override {
        if (!caller.hasCapability(taskCapabilityPci) || caller.serviceRole() != os::ServiceRole::Pci || request.length < 4) return -3;
        const auto operation = static_cast<os::PciOperation>(request.type);
        os::wire::PciFunctionInfo function{};
        MessageData::copy(&function, &request.data[1], sizeof(function));
        if (operation == os::PciOperation::Enumerate) {
            if (!request.data[0] || request.data[0] > 64) return -1;
            const uint32_t count = device::PciAccess::enumerate(reinterpret_cast<os::wire::PciFunctionInfo*>(&reply.data[1]), request.data[0]);
            reply.data[0] = count;
        } else if (operation == os::PciOperation::ReadConfig) {
            uint32_t value = 0;
            if (request.data[0] > UINT16_MAX || !device::PciAccess::read(function, request.data[0], value)) return -1;
            reply.data[1] = value;
        } else if (operation == os::PciOperation::WriteConfig) {
            if (request.length < 5 || request.data[0] > UINT16_MAX || request.data[3] > UINT32_MAX ||
                !device::ResourceDomain::writeConfiguration(request.data[4], function, request.data[0], request.data[3])) return -1;
        } else return -1;
        reply.length = os::ipcDataWordCount; return 0;
    }
};
}  // namespace
EndpointService& pciKernelService() {
    static PciService service;
    return service;
}
}  // namespace kernel
