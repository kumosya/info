/* 区域、驻留页和后备对象的初始状态。 */
#include "service/mm/mm.h"

namespace mm {

    /* 新区域保存所属任务、地址范围和访问权限。 */
    Region::Region(uint64_t owner, uint64_t start, uint64_t length,
                   uint32_t access)
        : task(owner),
          address(start),
          size(length),
          protection(access),
          maximumProtection(access),
          used(true) {
    }

    /* 新驻留记录关联任务中的页地址与内核页面句柄。 */
    ResidentPage::ResidentPage(uint64_t owner, uint64_t pageAddress,
                               uint64_t pageHandle)
        : task(owner), address(pageAddress), handle(pageHandle), used(true) {
    }

    /* 后备对象用独立编号连接区域记录与其数据来源。 */
    Backing::Backing(uint64_t number, BackingKind sourceKind,
                     uint64_t sourceHandle)
        : id(number), handle(sourceHandle), kind(sourceKind), used(true) {
    }

}  // namespace mm
