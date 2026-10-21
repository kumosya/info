#include "kernel/core_service.h"
#include "kernel/boot_image.h"
#include "os/pci.h"

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "kernel/new.h"
#include "kernel/thread.h"
#include "kernel/signal.h"

#include "kernel/service_message.h"
namespace kernel {
namespace {
class DeviceService final : public EndpointService {
   public:
    int call(Task& caller, const IpcMessage& request, IpcMessage& reply) override {
        const auto* args = request.data;
        const auto operation = static_cast<os::DeviceOperation>(request.type);
        if (operation == os::DeviceOperation::BootImageInfo || operation == os::DeviceOperation::BootImageRead || operation == os::DeviceOperation::BootImageWrite) {
            if (!caller.hasCapability(taskCapabilityDevice) || caller.serviceRole() != os::ServiceRole::Storage) return -3;
            return device::BootImage::call(request, reply);
        }
        if (request.length < 5) return -1;
        if (operation == os::DeviceOperation::Framebuffer) {
            os::DisplayInfo info{};
            if (!caller.hasCapability(taskCapabilityDisplay) || !device::DisplayAccess::mapFramebuffer(caller, args[0], info)) return -3;
            MessageData::copy(&reply.data[1], &info, sizeof(info));
        } else {
            // Only fixed boot identities receive device authority.
            if (!caller.hasCapability(taskCapabilityDevice) ||
                (caller.serviceRole() != os::ServiceRole::Device && caller.serviceRole() != os::ServiceRole::Storage)) return -3;
            switch (operation) {
                case os::DeviceOperation::InitializeInput:
                    if (caller.serviceRole() != os::ServiceRole::Device) return -3;
                    reply.data[0] = device::Ps2Access::initialize(); break;
                case os::DeviceOperation::ReadInput: {
                    if (caller.serviceRole() != os::ServiceRole::Device) return -3;
                    uint8_t value = 0; bool mouse = false;
                    if (!device::Ps2Access::read(value, mouse)) return -6;
                    reply.data[1] = value; reply.data[2] = mouse; break;
                }
                case os::DeviceOperation::MapMmio: {
                    os::PciFunctionInfo function{};
                    MessageData::copy(&function, &args[3], sizeof(function));
                    if (args[0] > 5) return -1;
                    auto* object = device::MmioRegion::map(caller, function, args[0], args[1], args[2]);
                    reply.data[0] = object ? caller.handles().add(object, HandleType::MmioRegion, 3) : 0;
                    if (object && !reply.data[0]) object->destroy();
                    MessageData::created(reply, reply.data[0]);
                    break;
                }
                case os::DeviceOperation::AllocateDma: {
                    HandleTable::Pin pin(caller.handles(), args[1], HandleType::DeviceDomain, handleRightManage);
                    auto* domain = pin.as<device::ResourceDomain>();
                    if (!domain) return -2;
                    os::DmaBufferInfo info{};
                    auto* object = device::DmaBuffer::create(caller, *domain, args[0], info);
                    if (!object) return -5;
                    info.handle = caller.handles().add(object, HandleType::DmaBuffer, 3);
                    if (!info.handle) { object->destroy(); return -5; }
                    MessageData::created(reply, info.handle);
                    MessageData::copy(&reply.data[1], &info, sizeof(info));
                    break;
                }
                case os::DeviceOperation::ClaimDevice: {
                    os::PciFunctionInfo function{};
                    MessageData::copy(&function, args, sizeof(function));
                    auto* domain = device::ResourceDomain::claim(caller, function);
                    if (!domain) return -3;
                    const auto handle = caller.handles().add(domain, HandleType::DeviceDomain, handleRightManage);
                    if (!handle) { domain->closeLease(); return -5; }
                    reply.data[0] = handle; MessageData::created(reply, handle); break;
                }
                case os::DeviceOperation::StopDevice: {
                    HandleTable::Pin pin(caller.handles(), args[0], HandleType::DeviceDomain, handleRightManage);
                    auto* domain = pin.as<device::ResourceDomain>();
                    if (!domain) return -2;
                    const int result = domain->stop(caller);
                    if (result) return result;
                    break;
                }
                case os::DeviceOperation::BindIrq: {
                    HandleTable::Pin pin(caller.handles(), args[1], HandleType::Endpoint, handleRightRead);
                    if (args[0] > 15 || args[2] > 1) return -1;
                    auto* object = device::InterruptBinding::create(caller, args[0], pin.as<Endpoint>(), args[2]);
                    reply.data[0] = object ? caller.handles().add(object, HandleType::InterruptBinding, 4) : 0;
                    if (object && !reply.data[0]) object->destroy();
                    MessageData::created(reply, reply.data[0]);
                    break;
                }
                case os::DeviceOperation::AcknowledgeIrq: {
                    HandleTable::Pin pin(caller.handles(), args[0], HandleType::InterruptBinding, handleRightManage);
                    auto* object = pin.as<device::InterruptBinding>();
                    if (!object || !object->acknowledge(caller)) return -2;
                    break;
                }
                default: return -1;
            }
        }
        reply.length = os::ipcDataWordCount;
        return 0;
    }
};

}  // namespace
EndpointService& deviceKernelService() {
    static DeviceService service;
    return service;
}
}  // namespace kernel
