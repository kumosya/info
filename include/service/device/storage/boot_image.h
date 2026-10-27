#include "service/common/channel.h"
#pragma once
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"

namespace storage {
// Only the reserved rootfs module is exposed by the kernel device endpoint.
class BootImageDriver {
    uint64_t blocks_ = 0;

   public:
    bool init() {
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(os::DeviceOperation::BootImageInfo);
        const auto result =
            services::RequestChannel(os::CoreServiceKind::Device)
                .result(request, reply);
        blocks_ = result > 0 ? result : 0;
        return blocks_ != 0;
    }
    uint64_t blockCount() const { return blocks_; }
    os::Storage::Error transfer(uint64_t block, uint32_t count, void* buffer,
                                bool write) {
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(write ? os::DeviceOperation::BootImageWrite
                                        : os::DeviceOperation::BootImageRead);
        request.data[0] = block;
        request.data[1] = count;
        request.length  = write ? 2 + count * 64 : 2;
        if (write)
            os::Runtime::copyBytes(&request.data[2], buffer, count * 512);
        if (services::RequestChannel(os::CoreServiceKind::Device)
                .result(request, reply) != 0)
            return os::Storage::Error::Io;
        if (!write) os::Runtime::copyBytes(buffer, &reply.data[2], count * 512);
        return os::Storage::Error::Success;
    }
};
}  // namespace storage
