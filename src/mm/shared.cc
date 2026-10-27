#include "service/mm/mm.h"
namespace mm {
int SharedMemoryManager::attach(Backing& backing, Region& region,
                                const os::IpcMessage& request) {
    if (request.handleCount != 1 || (request.data[3] & 4095) ||
        request.data[3] > UINT64_MAX - region.size)
        return -1;
    const auto rights = request.handleRights[0];
    if (!(rights & 1) || ((region.protection & 2) && !(rights & 2))) return -3;
    os::Ipc::Handle duplicateOwner;
    const int duplicateStatus = os::Ipc::Handle(request.handles[0], false)
                                    .duplicate(rights, 0, duplicateOwner);
    const int64_t duplicate =
        duplicateStatus ? duplicateStatus
                        : static_cast<int64_t>(duplicateOwner.release());
    if (duplicate <= 0) return -2;
    backing.handle           = duplicate;
    backing.writable         = rights & 2;
    region.maximumProtection = backing.writable ? 7 : 5;
    region.offset            = request.data[3];
    region.shared            = true;
    return 0;
}
}  // namespace mm
