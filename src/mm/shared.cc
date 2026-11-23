/*
 * src/mm/shared.cc
 * 解析共享对象的映射参数，并建立区域与后备对象的关联。
 */

#include "service/mm/mm.h"

namespace mm {

    /*
     * 解析共享内存请求中的句柄和偏移，检查对象大小，再把区域关联到该后备对象。
     * 取得的引用由后备记录统一管理。
     */
    int SharedMemoryManager::attach(Backing& backing, Region& region,
                                    const os::IpcMessage& request) {
        if (request.handleCount != 1 || (request.data[3] & 4095) ||
            request.data[3] > UINT64_MAX - region.size) {
            return -1;
        }

        const uint32_t rights = request.handleRights[0];

        // 共享区域至少要可读，申请可写映射时还必须证明传入句柄确实带写权限。
        if (!(rights & 1) || ((region.protection & 2) && !(rights & 2))) {
            return -3;
        }

        // 请求携带句柄会在本轮结束关闭，后备记录需要先取得自己长期拥有的一份复制引用。
        os::Ipc::Handle duplicateOwner;
        const int duplicateStatus = os::Ipc::Handle(request.handles[0], false)
                                        .duplicate(rights, 0, duplicateOwner);
        const int64_t duplicate =
            duplicateStatus ? duplicateStatus
                            : static_cast<int64_t>(duplicateOwner.release());

        if (duplicate <= 0) {
            return -2;
        }

        backing.handle   = duplicate;
        backing.writable = rights & 2;

        // 最高权限来源于原句柄是否可写，后续 mprotect
        // 不能把只读共享对象提升成可写。
        region.maximumProtection = backing.writable ? 7 : 5;
        region.offset            = request.data[3];
        region.shared            = true;

        return 0;
    }

}  // namespace mm
