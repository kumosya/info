#include "service/common/records.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "kernel/ipc.h"
#include "kernel/boot_image.h"
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
            os::wire::DisplayInfo info{};
            if (!caller.hasCapability(taskCapabilityDisplay))return -3;
            auto* mapping=device::FrameMapping::create(caller,args[0],info);if(!mapping)return -5;
            const uint64_t handle=caller.handles().add(mapping,HandleType::FramebufferMapping,handleRightRead|handleRightManage);
            if(!handle){mapping->destroy();return -5;}
            caller.handles().setFlags(handle,os::handleFlagCloseOnExec);
            reply.data[0]=handle;MessageData::created(reply,handle);
            MessageData::copy(&reply.data[1], &info, sizeof(info));
        } else if(operation==os::DeviceOperation::FrameInfo) {
            HandleTable::Pin pin(caller.handles(),args[0],HandleType::FramebufferMapping,handleRightRead);
            auto* mapping=pin.as<device::FrameMapping>();os::wire::DisplayInfo info{};
            if(!mapping || !mapping->query(caller,info))return -2;
            MessageData::copy(&reply.data[1],&info,sizeof(info));
        } else {
            // Only fixed boot identities receive device authority.
            if (!caller.hasCapability(taskCapabilityDevice) ||
                (caller.serviceRole() != os::ServiceRole::Device && caller.serviceRole() != os::ServiceRole::Storage)) return -3;
            switch (operation) {
                case os::DeviceOperation::InitializeInput:
                    if (caller.serviceRole() != os::ServiceRole::Device) return -3;
                    reply.data[0] = device::Ps2Access::init(); break;
                case os::DeviceOperation::ReadInput: {
                    if (caller.serviceRole() != os::ServiceRole::Device) return -3;
                    uint8_t value = 0; bool mouse = false;
                    if (!device::Ps2Access::read(value, mouse)) return -6;
                    reply.data[1] = value; reply.data[2] = mouse; break;
                }
                case os::DeviceOperation::MapMmio: {
                    os::wire::PciFunctionInfo function{};
                    MessageData::copy(&function, &args[3], sizeof(function));
                    if (args[0] > 5) return -1;
                    auto* object = device::MmioRegion::map(caller, function, args[0], args[1], args[2]);
                    reply.data[0] = object ? caller.handles().add(object, HandleType::MmioRegion, 3) : 0;
                    if (object && !reply.data[0]) object->destroy();
                    if(reply.data[0])caller.handles().setFlags(reply.data[0],os::handleFlagCloseOnExec);
                    MessageData::created(reply, reply.data[0]);
                    break;
                }
                case os::DeviceOperation::MapBar: {
                    if(request.length<6 || args[0]>=6 || !args[1] || args[1]>UINT64_MAX-4095 || (args[2]&4095))return -1;
                    HandleTable::Pin pin(caller.handles(),args[5],HandleType::DeviceDomain,handleRightManage);
                    auto* domain=pin.as<device::ResourceDomain>();if(!domain || !domain->active(caller))return -2;
                    os::wire::PciFunctionInfo function{};MessageData::copy(&function,&args[3],sizeof(function));
                    auto* object=device::MmioRegion::map(caller,function,args[0],0,(args[1]+4095)&~4095ULL,args[2]);
                    if(!object)return -5;
                    const uint64_t handle=caller.handles().add(object,HandleType::MmioRegion,3);
                    if(!handle){object->destroy();return -5;}
                    caller.handles().setFlags(handle,os::handleFlagCloseOnExec);
                    reply.data[0]=handle;reply.data[1]=object->address();reply.data[2]=object->size();MessageData::created(reply,handle);break;
                }
                case os::DeviceOperation::CreateDma:
                case os::DeviceOperation::AllocateDma: {
                    HandleTable::Pin pin(caller.handles(), args[1], HandleType::DeviceDomain, handleRightManage);
                    auto* domain = pin.as<device::ResourceDomain>();
                    if (!domain) return -2;
                    os::wire::DmaBufferInfo info{};
                    auto* object = operation==os::DeviceOperation::CreateDma ? device::DmaBuffer::create(caller,*domain,0,info,args[0],args[2]) : device::DmaBuffer::create(caller, *domain, args[0], info);
                    if (!object) return -5;
                    info.handle = caller.handles().add(object, HandleType::DmaBuffer, 3);
                    if (!info.handle) { object->destroy(); return -5; }
                    caller.handles().setFlags(info.handle,os::handleFlagCloseOnExec);
                    MessageData::created(reply, info.handle);
                    MessageData::copy(&reply.data[1], &info, sizeof(info));
                    break;
                }
                case os::DeviceOperation::ClaimDevice: {
                    os::wire::PciFunctionInfo function{};
                    MessageData::copy(&function, args, sizeof(function));
                    auto* domain = device::ResourceDomain::claim(caller, function);
                    if (!domain) return -3;
                    const auto handle = caller.handles().add(domain, HandleType::DeviceDomain, handleRightManage);
                    if (!handle) { domain->closeLease(); return -5; }
                    caller.handles().setFlags(handle,os::handleFlagCloseOnExec);
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
                    HandleTable::Pin domainPin(caller.handles(),args[3],HandleType::DeviceDomain,handleRightManage);
                    if(caller.serviceRole()==os::ServiceRole::Storage) {auto* domain=domainPin.as<device::ResourceDomain>();if(!domain || !domain->allowsIrq(caller,args[0]))return -3;}
                    else if(args[0]!=1 && args[0]!=12)return -3;
                    auto* object = device::InterruptBinding::create(caller, args[0], pin.as<Endpoint>(), args[2]);
                    reply.data[0] = object ? caller.handles().add(object, HandleType::InterruptBinding, 4) : 0;
                    if (object && !reply.data[0]) object->destroy();
                    if(reply.data[0])caller.handles().setFlags(reply.data[0],os::handleFlagCloseOnExec);
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
