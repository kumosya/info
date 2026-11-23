/*
 * 校验 PCI 服务身份，分发枚举和受限配置操作。
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/pci.h"
#include "kernel/smp.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include <os/device.h>

namespace task {

    namespace PciService {

        /*
         * 检查 PCI
         * 能力后分发设备枚举、配置读取和写入请求，写配置还要经过设备资源归属规则。
         */
        int call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
            // 配置空间是硬件控制入口，先核对固定 PCI
            // 服务身份、能力和消息最小长度。
            if (!caller.hasCapability(taskCapabilityPci) ||
                caller.serviceRole() != os::ServiceRole::Pci ||
                request.length < 4) {
                return -3;
            }

            const os::PciOperation operation =
                static_cast<os::PciOperation>(request.type);
            os::wire::PciFunctionInfo function{};
            copyMessageData(&function, &request.data[1], sizeof(function));

            if (operation == os::PciOperation::Enumerate) {
                // 枚举容量限制为回复消息能容纳的范围，不能让总线扫描越过回复数组。
                if (!request.data[0] || request.data[0] > 64) {
                    return -1;
                }

                const uint32_t count = device::pciBus().enumerate(
                    reinterpret_cast<os::wire::PciFunctionInfo*>(
                        &reply.data[1]),
                    request.data[0]);
                reply.data[0] = count;
            } else if (operation == os::PciOperation::ReadConfig) {
                uint32_t value = 0;

                if (request.data[0] > UINT16_MAX ||
                    !device::Pci(device::pciBus(), function)
                         .read(request.data[0], value)) {
                    return -1;
                }

                reply.data[1] = value;
            } else if (operation == os::PciOperation::WriteConfig) {
                // 写请求必须通过资源域的所有权规则，不能让 PCI
                // 代理绕过设备停机保护重新开启 DMA。
                if (request.length < 5 || request.data[0] > UINT16_MAX ||
                    request.data[3] > UINT32_MAX ||
                    !device::resourceDomains().writeConfiguration(
                        request.data[4], function, request.data[0],
                        request.data[3])) {
                    return -1;
                }
            } else {
                return -1;
            }

            reply.length = os::ipcDataWordCount;

            return 0;
        }

    }  // namespace PciService

    /*
     * 返回 PCI 内核服务入口描述，供核心服务端点调用。
     */
    EndpointService& pciKernelService() {
        static EndpointService service(PciService::call);

        return service;
    }

}  // namespace task
