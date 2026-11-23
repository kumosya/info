/*
 * 分配设备映射地址，验证 BAR 边界并管理用户寄存器映射。
 */

#include <new>

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"

namespace device {

    /*
     * 从专用用户地址区间原子预留整页空间，并额外留一页间隔；只分配地址编号，不立即建立映射。
     */
    uint64_t reserveDisplayAddress(uint64_t size) {
        static uint64_t next = 0x0000020000000000ULL;

        if (!size || size > UINT64_MAX - 8191) {
            return 0;
        }

        // 映射长度取整后多留一个空页作为间隔；这里仅分配虚拟地址编号，并不建立页表映射。
        const uint64_t bytes = ((size + 4095) & ~4095ULL) + 4096;
        uint64_t start       = __atomic_load_n(&next, __ATOMIC_RELAXED);

        for (;;) {
            if (start > 0x0000700000000000ULL - bytes) {
                return 0;
            }

            const uint64_t end = start + bytes;

            // 多线程用比较交换领取互不重叠的区间；失败时 start
            // 被刷新，再按最新位置重试。
            if (__atomic_compare_exchange_n(&next, &start, end, false,
                                            __ATOMIC_RELAXED,
                                            __ATOMIC_RELAXED)) {
                return start;
            }
        }
    }

    /*
     * 在已认领的设备域内验证 BAR
     * 偏移和长度，逐页映射设备寄存器并登记包装；任何失败都撤销本次映射，最后归还查询域的临时引用。
     */
    MmioRegion* MmioRegion::map(::task::Task& task,
                                const os::wire::PciFunctionInfo& function,
                                uint8_t barIndex, uint64_t userAddress,
                                uint64_t size, uint64_t offset) {
        if (!userAddress) {
            userAddress = reserveDisplayAddress(size);
        }

        if (barIndex >= 6 || (userAddress & (mm::physicalPageSize - 1)) != 0 ||
            userAddress < 0x10000 || size == 0 ||
            userAddress + size < userAddress ||
            userAddress + size >= 0x0000800000000000ULL ||
            (size & (mm::physicalPageSize - 1)) != 0) {
            return nullptr;
        }

        // 查找确认该任务确实拥有活动设备，并临时持有资源域，避免映射过程中域被回收。
        ResourceDomain* domain = resourceDomains().find(task, function);

        if (!domain) {
            return nullptr;
        }

        MmioRegion* region = ([&]() -> MmioRegion* {
            // 对偏移和长度使用减法检查，确保所映射部分完全落在已验证的 BAR
            // 窗口内。
            const uint64_t barBase = domain->barBases_[barIndex],
                           barSize = domain->barSizes_[barIndex];

            if (!barBase || offset > barSize || size > barSize - offset ||
                (offset & 4095) || (barBase & (mm::physicalPageSize - 1))) {
                return nullptr;
            }

            ::task::PreemptionSpinLockGuard guard(task.addressSpace().lock());

            if (!task.addressSpace().rootPhysicalAddress()) {
                return nullptr;
            }

            // 把设备寄存器页映射为用户可写、不可执行、禁缓存的外部页；这些页不由普通页分配器拥有。
            for (uint64_t pageOffset = 0; pageOffset < size;
                 pageOffset += mm::physicalPageSize) {
                if (!mm::mapPageInAddressSpace(
                        task.addressSpace().rootPhysicalAddress(),
                        userAddress + pageOffset, barBase + offset + pageOffset,
                        ::mm::pageUser | ::mm::pageWritable |
                            ::mm::pageNoExecute | ::mm::pageCacheDisable |
                            ::mm::pageExternal)) {
                    // 中途失败只撤销本轮成功的前缀，保留原来不属于本轮的地址内容。
                    while (pageOffset != 0) {
                        pageOffset -= mm::physicalPageSize;
                        mm::unmapPageInAddressSpace(
                            task.addressSpace().rootPhysicalAddress(),
                            userAddress + pageOffset);
                    }

                    return nullptr;
                }
            }

            // 全部页映射成功后再申请包装；对象分配失败时撤销刚才建立的全部页。
            void* storage = mm::allocateHeap(sizeof(MmioRegion));
            MmioRegion* region =
                storage == nullptr ? nullptr : new (storage) MmioRegion;

            if (region == nullptr) {
                for (uint64_t pageOffset = 0; pageOffset < size;
                     pageOffset += mm::physicalPageSize) {
                    mm::unmapPageInAddressSpace(
                        task.addressSpace().rootPhysicalAddress(),
                        userAddress + pageOffset);
                }

                return nullptr;
            }

            region->physicalAddress_ = barBase + offset;
            region->virtualAddress_  = userAddress;
            region->size_            = size;
            region->mapped_          = true;

            // attach 是长期归属交接，若设备已经开始停机则回滚映射并释放包装。
            if (!domain->attach(*region)) {
                for (uint64_t pageOffset = 0; pageOffset < size;
                     pageOffset += mm::physicalPageSize) {
                    mm::unmapPageInAddressSpace(
                        task.addressSpace().rootPhysicalAddress(),
                        userAddress + pageOffset);
                }

                mm::freeHeap(region);

                return nullptr;
            }

            return region;
        }());

        // 无论内部构造成功还是提前失败，都经过这里归还 find 取得的临时引用。
        domain->drop();

        return region;
    }

    /*
     * 返回这份 MMIO 映射的用户虚拟地址。
     */
    uint64_t MmioRegion::address() const { return virtualAddress_; }

    /*
     * 返回这份 MMIO 映射覆盖的字节数。
     */
    uint64_t MmioRegion::size() const { return size_; }

    /*
     * 仅一次撤销用户映射并归还资源域引用，包装最终释放由域的后台清理负责。
     */
    void MmioRegion::destroy() {
        // 用原子关闭标志防止重复处理；只撤销用户访问并归还引用，包装由资源域统一回收。
        if (__atomic_exchange_n(&closed_, true, __ATOMIC_ACQ_REL)) {
            return;
        }

        domain_->unmap(virtualAddress_, size_, mapped_, physicalAddress_);
        domain_->drop();
    }

}  // namespace device
