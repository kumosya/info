#include "kernel/boot.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/sync.h"
#include "service/common/kernel-requests.h"
#include <os/storage.h>

namespace kernel::device {

namespace {
BootImage image;
}
BootImage& bootImage() { return image; }
void BootImage::init(const BootInfo& boot) {
    for (uint32_t i = 0; i < boot.getModuleCount(); ++i) {
        const auto& module    = boot.getModules()[i];
        const char* name      = module.getCommandLine();
        const char expected[] = "rootfs";
        uint32_t n            = 0;
        while (name && expected[n] && name[n] == expected[n]) ++n;
        if (n != sizeof(expected) - 1 || (name[n] && name[n] != ' ') ||
            module.getEnd() <= module.getStart())
            continue;
        bytes = reinterpret_cast<uint8_t*>(0xffff800000000000ULL +
                                           module.getStart());
        size  = module.getEnd() - module.getStart();
        break;
    }
}
int BootImage::call(const ::kernel::task::IpcMessage& request,
                    ::kernel::task::IpcMessage& reply) {
    const auto operation = static_cast<os::DeviceOperation>(request.type);
    if (operation == os::DeviceOperation::BootImageInfo) {
        reply.data[0] = size / 512;
        reply.length  = 1;
        return 0;
    }
    if (request.length < 2) return -1;
    const uint64_t block = request.data[0], count = request.data[1];
    if (!bytes || count == 0 || count > os::maximumStorageTransferBlocks ||
        block >= size / 512 || count > size / 512 - block)
        return -1;
    const bool write = operation == os::DeviceOperation::BootImageWrite;
    if (write && request.length < 2 + count * 64) return -1;
    ::kernel::task::InterruptSpinLockGuard guard(lock);
    if (write)
        ::kernel::task::copyMessageData(bytes + block * 512, &request.data[2],
                                        count * 512);
    else
        ::kernel::task::copyMessageData(&reply.data[2], bytes + block * 512,
                                        count * 512);
    reply.data[0] = 0;
    reply.data[1] = count * 512;
    reply.length  = write ? 2 : 2 + count * 64;
    return 0;
}

}  // namespace kernel::device
