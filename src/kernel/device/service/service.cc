/*
 * 校验设备请求权限，以句柄管理映射、DMA 与中断资源。
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
#include "service/common/identity.h"
#include "service/common/protocol.h"
#include "service/common/records.h"
#include <os/device.h>

namespace task {

    namespace device_service {

        /*
         * 校验设备能力、固定服务身份和消息格式，再分发显示、MMIO、DMA、中断及资源域请求；新对象句柄安装失败时归还刚创建的资源。
         */
        int call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
            const uint64_t* args = request.data;
            const services::DeviceOperation operation =
                static_cast<services::DeviceOperation>(request.type);

            // 启动磁盘代理只向固定存储服务开放；它的消息布局独立，先于一般设备参数检查分派。
            if (operation == services::DeviceOperation::BootImageInfo ||
                operation == services::DeviceOperation::BootImageRead ||
                operation == services::DeviceOperation::BootImageWrite) {
                if (!caller.hasCapability(taskCapabilityDevice) ||
                    caller.serviceRole() != services::ServiceRole::Storage) {
                    return -3;
                }

                return device::bootImage().call(request, reply);
            }

            if (request.length < 5) {
                return -1;
            }

            // 显示能力与普通设备控制能力分别检查，成功映射后用句柄管理这段映射的生命周期。
            if (operation == services::DeviceOperation::Framebuffer) {
                services::wire::DisplayInfo info{};

                if (!caller.hasCapability(services::taskCapabilityDisplay)) {
                    return -3;
                }

                ::device::FrameMapping* mapping =
                    device::FrameMapping::create(caller, args[0], info);

                if (!mapping) {
                    return -5;
                }

                const uint64_t handle =

                    // 句柄登记失败必须销毁已建立的映射，不能把没有句柄可关闭的资源遗留在任务中。
                    caller.handles().add(mapping,
                                         HandleType::FramebufferMapping,
                                         handleRightRead | handleRightManage);

                if (!handle) {
                    mapping->destroy();

                    return -5;
                }

                caller.handles().setFlags(handle, services::handleFlagCloseOnExec);
                reply.data[0] = handle;
                addCreatedHandle(reply, handle);
                copyMessageData(&reply.data[1], &info, sizeof(info));
            } else if (operation == services::DeviceOperation::FrameInfo) {
                // 查询期间用 Pin
                // 保持句柄对象有效，并检查读权限与旧映像映射是否仍存在。
                HandleTable::Pin pin(caller.handles(), args[0],
                                     HandleType::FramebufferMapping,
                                     handleRightRead);
                device::FrameMapping* mapping = pin.as<device::FrameMapping>();
                services::wire::DisplayInfo info{};

                if (!mapping || !mapping->query(caller, info)) {
                    return -2;
                }

                copyMessageData(&reply.data[1], &info, sizeof(info));
            } else {
                // 只有内核配置的固定启动服务身份能获得设备控制权。
                if (!caller.hasCapability(taskCapabilityDevice) ||
                    (caller.serviceRole() != services::ServiceRole::Device &&
                     caller.serviceRole() != services::ServiceRole::Storage)) {
                    return -3;
                }

                switch (operation) {
                    // IDE 端口请求必须持有该控制器的活动管理句柄，数据最多为 512 字节。
                    case services::DeviceOperation::PortIo: {
                        if (args[1] > UINT16_MAX || (args[2] != 1 && args[2] != 2) ||
                            !args[3] || args[3] > 256 || args[4] > 1 ||
                            (args[4] && request.length < 5 + (args[2] * args[3] + 7) / 8)) {
                            return -1;
                        }
                        HandleTable::Pin pin(caller.handles(), args[0],
                                             HandleType::DeviceDomain, handleRightManage);
                        device::ResourceDomain* domain = pin.as<device::ResourceDomain>();
                        if (!domain) {
                            return -2;
                        }
                        uint8_t* bytes = reinterpret_cast<uint8_t*>(&reply.data[1]);
                        if (args[4]) {
                            copyMessageData(bytes, &args[5], args[2] * args[3]);
                        }
                        const int result = domain->portIo(caller, args[1], args[2],
                                                           args[3], args[4], bytes);
                        if (result) {
                            return result;
                        }
                        break;
                    }
                    // 输入初始化只给固定设备服务，存储服务即使有设备能力也不能操作键盘鼠标控制器。
                    case services::DeviceOperation::InitializeInput:
                        if (caller.serviceRole() != services::ServiceRole::Device) {
                            return -3;
                        }

                        reply.data[0] = device::initPs2();
                        break;
                    case services::DeviceOperation::ReadInput: {
                        if (caller.serviceRole() != services::ServiceRole::Device) {
                            return -3;
                        }

                        uint8_t value = 0;
                        bool mouse    = false;

                        if (!device::readPs2(value, mouse)) {
                            return -6;
                        }

                        reply.data[1] = value;
                        reply.data[2] = mouse;
                        break;
                    }

                    // 兼容接口允许调用者给出用户地址，实际映射仍要验证设备归属与
                    // BAR 边界。
                    case services::DeviceOperation::MapMmio: {
                        services::wire::PciFunctionInfo function{};
                        copyMessageData(&function, &args[3], sizeof(function));

                        if (args[0] > 5) {
                            return -1;
                        }

                        void* storage = mm::allocateHeap(sizeof(device::MmioRegion));
                        device::MmioRegion* object = storage ? new (storage) device::MmioRegion : nullptr;
                        if (object && !object->map(caller, function, args[0], args[1], args[2])) {
                            mm::freeHeap(object);
                            object = nullptr;
                        }
                        reply.data[0] =
                            object ? caller.handles().add(
                                         object, HandleType::MmioRegion, 3)
                                   : 0;

                        if (object && !reply.data[0]) {
                            object->destroy();
                        }

                        if (reply.data[0]) {
                            caller.handles().setFlags(
                                reply.data[0], services::handleFlagCloseOnExec);
                        }

                        addCreatedHandle(reply, reply.data[0]);
                        break;
                    }

                    // 新接口由内核选地址；先检查长度和偏移，再固定设备域句柄，避免关闭并发导致对象失效。
                    case services::DeviceOperation::MapBar: {
                        if (request.length < 6 || args[0] >= 6 || !args[1] ||
                            args[1] > UINT64_MAX - 4095 || (args[2] & 4095)) {
                            return -1;
                        }

                        HandleTable::Pin pin(caller.handles(), args[5],
                                             HandleType::DeviceDomain,
                                             handleRightManage);
                        device::ResourceDomain* domain =
                            pin.as<device::ResourceDomain>();

                        if (!domain || !domain->active(caller)) {
                            return -2;
                        }

                        services::wire::PciFunctionInfo function{};
                        copyMessageData(&function, &args[3], sizeof(function));
                        void* storage = mm::allocateHeap(sizeof(device::MmioRegion));
                        device::MmioRegion* object = storage ? new (storage) device::MmioRegion : nullptr;
                        if (object && !object->map(caller, function, args[0], 0,
                                                   (args[1] + 4095) & ~4095ULL, args[2])) {
                            mm::freeHeap(object);
                            object = nullptr;
                        }

                        if (!object) {
                            return -5;
                        }

                        const uint64_t handle = caller.handles().add(
                            object, HandleType::MmioRegion, 3);

                        if (!handle) {
                            object->destroy();

                            return -5;
                        }

                        caller.handles().setFlags(handle,
                                                  services::handleFlagCloseOnExec);
                        reply.data[0] = handle;
                        reply.data[1] = object->address();
                        reply.data[2] = object->size();
                        addCreatedHandle(reply, handle);
                        break;
                    }

                    // 两种 DMA 接口共用分配路径，成功后把设备地址和 CPU
                    // 地址连同句柄一起返回。
                    case services::DeviceOperation::CreateDma:
                    case services::DeviceOperation::AllocateDma: {
                        HandleTable::Pin pin(caller.handles(), args[1],
                                             HandleType::DeviceDomain,
                                             handleRightManage);
                        device::ResourceDomain* domain =
                            pin.as<device::ResourceDomain>();

                        if (!domain) {
                            return -2;
                        }

                        services::wire::DmaBufferInfo info{};
                        void* storage = mm::allocateHeap(sizeof(device::DmaBuffer));
                        device::DmaBuffer* object = storage ? new (storage) device::DmaBuffer : nullptr;
                        if (object) {
                            const bool created = operation == services::DeviceOperation::CreateDma
                                ? object->create(caller, *domain, 0, info, args[0], args[2])
                                : object->create(caller, *domain, args[0], info);
                            if (!created) {
                                mm::freeHeap(object);
                                object = nullptr;
                            }
                        }

                        if (!object) {
                            return -5;
                        }

                        info.handle = caller.handles().add(
                            object, HandleType::DmaBuffer, 3);

                        if (!info.handle) {
                            object->destroy();

                            return -5;
                        }

                        caller.handles().setFlags(info.handle,
                                                  services::handleFlagCloseOnExec);
                        addCreatedHandle(reply, info.handle);
                        copyMessageData(&reply.data[1], &info, sizeof(info));
                        break;
                    }

                    // 认领域成功后还必须创建管理句柄；若失败，关闭租约以触发设备停机。
                    case services::DeviceOperation::ClaimDevice: {
                        services::wire::PciFunctionInfo function{};
                        copyMessageData(&function, args, sizeof(function));
                        ::device::ResourceDomain* domain =
                            device::resourceDomains().claim(caller, function);

                        if (!domain) {
                            return -3;
                        }

                        const uint64_t handle = caller.handles().add(
                            domain, HandleType::DeviceDomain,
                            handleRightManage);

                        if (!handle) {
                            domain->closeLease();

                            return -5;
                        }

                        caller.handles().setFlags(handle,
                                                  services::handleFlagCloseOnExec);
                        reply.data[0] = handle;
                        addCreatedHandle(reply, handle);
                        break;
                    }

                    // 停机操作等待资源域确认，错误直接传回，不能把超时或隔离伪装为成功。
                    case services::DeviceOperation::StopDevice: {
                        HandleTable::Pin pin(caller.handles(), args[0],
                                             HandleType::DeviceDomain,
                                             handleRightManage);
                        device::ResourceDomain* domain =
                            pin.as<device::ResourceDomain>();

                        if (!domain) {
                            return -2;
                        }

                        const int result = domain->stop(caller);

                        if (result) {
                            return result;
                        }

                        break;
                    }

                    // 存储服务只能绑定自身设备声明的
                    // IRQ；输入服务只允许键盘一号和鼠标十二号。
                    case services::DeviceOperation::BindIrq: {
                        HandleTable::Pin pin(caller.handles(), args[1],
                                             HandleType::Endpoint,
                                             handleRightRead);

                        if (args[0] > 15 || args[2] > 1) {
                            return -1;
                        }

                        HandleTable::Pin domainPin(caller.handles(), args[3],
                                                   HandleType::DeviceDomain,
                                                   handleRightManage);

                        if (caller.serviceRole() == services::ServiceRole::Storage) {
                            device::ResourceDomain* domain =
                                domainPin.as<device::ResourceDomain>();

                            if (!domain ||
                                !domain->allowsIrq(caller, args[0])) {
                                return -3;
                            }
                        } else if (args[0] != 1 && args[0] != 12) {
                            return -3;
                        }

                        void* storage = mm::allocateHeap(sizeof(device::InterruptBinding));
                        device::InterruptBinding* object = storage ? new (storage) device::InterruptBinding : nullptr;
                        if (object && !object->create(caller, args[0], pin.as<Endpoint>(), args[2])) {
                            mm::freeHeap(object);
                            object = nullptr;
                        }
                        reply.data[0] =
                            object
                                ? caller.handles().add(
                                      object, HandleType::InterruptBinding, 4)
                                : 0;

                        if (object && !reply.data[0]) {
                            object->destroy();
                        }

                        if (reply.data[0]) {
                            caller.handles().setFlags(
                                reply.data[0], services::handleFlagCloseOnExec);
                        }

                        addCreatedHandle(reply, reply.data[0]);
                        break;
                    }

                    // 确认必须持有管理权限且属于绑定所有者；电平 IRQ
                    // 在这里重新开放。
                    case services::DeviceOperation::AcknowledgeIrq: {
                        HandleTable::Pin pin(caller.handles(), args[0],
                                             HandleType::InterruptBinding,
                                             handleRightManage);
                        device::InterruptBinding* object =
                            pin.as<device::InterruptBinding>();

                        if (!object || !object->acknowledge(caller)) {
                            return -2;
                        }

                        break;
                    }

                    default:
                        return -1;
                }
            }

            // 通用回复按完整数据区长度返回；需要上层接管的新句柄已登记到回复的句柄列表。
            reply.length = os::ipcDataWordCount;

            return 0;
        }

    }  // namespace device_service

    /*
     * 返回设备内核服务入口描述，供核心服务表建立端点。
     */
    EndpointService& deviceKernelService() {
        static EndpointService service(device_service::call);

        return service;
    }

}  // namespace task
