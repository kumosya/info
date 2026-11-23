/*
 * 把 rootfs 引导模块作为可读写的内存块设备。
 */

#include "kernel/boot.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/sync.h"
#include "service/common/kernel-requests.h"
#include <os/storage.h>

namespace device {

    static BootImage image;

    /*
     * 返回以引导内存模块为后备的磁盘映像服务对象。
     */
    BootImage& bootImage() { return image; }

    /*
     * 查找名称为 rootfs
     * 的有效引导模块，保存其直接映射地址和大小，不复制整个磁盘内容。
     */
    void BootImage::init(const BootInfo& boot) {
        for (uint32_t i = 0; i < boot.getModuleCount(); ++i) {
            const BootModule& module = boot.getModules()[i];
            const char* name         = module.getCommandLine();
            const char expected[]    = "rootfs";
            uint32_t n               = 0;

            // 完整匹配 rootfs
            // 模块名，允许后接空格参数，不能把相同前缀的其他模块误认成磁盘。
            while (name && expected[n] && name[n] == expected[n]) {
                ++n;
            }

            if (n != sizeof(expected) - 1 || (name[n] && name[n] != ' ') ||
                module.getEnd() <= module.getStart()) {
                continue;
            }

            // 引导模块正文通过高地址直接映射访问；后面的读写改动的是内存中的启动镜像。
            bytes = reinterpret_cast<uint8_t*>(0xffff800000000000ULL +
                                               module.getStart());
            size  = module.getEnd() - module.getStart();
            break;
        }
    }

    /*
     * 按 512 字节块查询或读写引导映像。
     * 先核对消息与映像边界，再在锁内复制数据，返回实际传输长度。
     */
    int BootImage::call(const ::task::IpcMessage& request,
                        ::task::IpcMessage& reply) {
        const os::DeviceOperation operation =
            static_cast<os::DeviceOperation>(request.type);

        // 存储协议以五百一十二字节块计量，信息请求只返回完整块数。
        if (operation == os::DeviceOperation::BootImageInfo) {
            reply.data[0] = size / 512;
            reply.length  = 1;

            return 0;
        }

        if (request.length < 2) {
            return -1;
        }

        const uint64_t block = request.data[0], count = request.data[1];

        // 用剩余块数验证范围，避免起始块加数量溢出；单次传输还受消息容量限制。
        if (!bytes || count == 0 || count > os::maximumStorageTransferBlocks ||
            block >= size / 512 || count > size / 512 - block) {
            return -1;
        }

        const bool write = operation == os::DeviceOperation::BootImageWrite;

        if (write && request.length < 2 + count * 64) {
            return -1;
        }

        // 复制期间串行访问镜像；写数据取自请求，读数据放入回复，字数按每块六十四个八字节字计算。
        ::task::InterruptSpinLockGuard guard(lock);

        if (write) {
            ::task::copyMessageData(bytes + block * 512, &request.data[2],
                                    count * 512);
        } else {
            ::task::copyMessageData(&reply.data[2], bytes + block * 512,
                                    count * 512);
        }

        reply.data[0] = 0;
        reply.data[1] = count * 512;
        reply.length  = write ? 2 : 2 + count * 64;

        return 0;
    }

}  // namespace device
