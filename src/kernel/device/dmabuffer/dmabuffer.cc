/*
 * 为设备分配连续物理内存，建立用户映射并交给资源域管理。
 */

#include <new>

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"

namespace device {

    /*
     * 为活动设备资源域分配满足大小和对齐要求的连续物理块，映射到任务并清零；完成登记后返回
     * CPU 地址与设备地址，失败时撤销本次资源。
     */
    DmaBuffer* DmaBuffer::create(::task::Task& task, ResourceDomain& domain,
                                 uint64_t userAddress,
                                 os::wire::DmaBufferInfo& info, uint64_t size,
                                 uint64_t alignment) {
        if (!size || size > 8 * 1024 * 1024ULL || !alignment ||
            (alignment & (alignment - 1)) || alignment > 8 * 1024 * 1024ULL ||
            !domain.active(task)) {
            return nullptr;
        }

        // 连续物理块按二次幂页数分配，块大小必须同时满足长度和对齐要求，实际返回容量可能大于请求。
        uint32_t order = 0;

        while ((4096ULL << order) < size || (4096ULL << order) < alignment) {
            ++order;
        }

        const uint64_t bytes = 4096ULL << order;

        if (!userAddress) {
            userAddress = reserveDisplayAddress(bytes);
        }

        if ((userAddress & 4095) || userAddress < 0x10000 ||
            userAddress > 0x800000000000ULL - bytes) {
            return nullptr;
        }

        // 锁住任务地址空间后再取页表根，避免映射途中与地址空间替换交错。
        ::task::PreemptionSpinLockGuard guard(task.addressSpace().lock());
        const uint64_t root = task.addressSpace().rootPhysicalAddress();

        if (!root) {
            return nullptr;
        }

        const uint64_t physical = mm::allocatePhysicalBlock(order, task.id());

        if (!physical) {
            return nullptr;
        }

        // 逐页映射同一个连续物理块，记录成功长度；失败时逆序撤销，再归还整块物理内存。
        uint64_t mapped = 0;

        for (; mapped < bytes; mapped += 4096) {
            if (!mm::mapPageInAddressSpace(
                    root, userAddress + mapped, physical + mapped,
                    ::mm::pageUser | ::mm::pageWritable | ::mm::pageNoExecute |
                        ::mm::pageExternal)) {
                break;
            }
        }

        if (mapped != bytes) {
            while (mapped) {
                mapped -= 4096;
                mm::unmapPageInAddressSpace(root, userAddress + mapped);
            }

            mm::releasePhysicalBlock(physical, order, task.id());

            return nullptr;
        }

        // 页面准备完才创建句柄背后的对象；对象分配失败同样需要撤销映射并归还物理块。
        void* storage = mm::allocateHeap(sizeof(DmaBuffer));
        ::device::DmaBuffer* buffer =
            storage ? new (storage) DmaBuffer : nullptr;

        if (!buffer) {
            for (uint64_t i = 0; i < bytes; i += 4096) {
                mm::unmapPageInAddressSpace(root, userAddress + i);
            }

            mm::releasePhysicalBlock(physical, order, task.id());

            return nullptr;
        }

        buffer->physicalAddress_ = physical;
        buffer->virtualAddress_  = userAddress;
        buffer->size_            = bytes;
        buffer->mapped_          = true;

        // 交给设备服务前清零，避免泄露原来的内存内容。
        mm::clearPhysicalPages(physical, bytes);

        // 登记是最终归属交接；资源域可能已开始停机，拒绝登记时要完整回滚本次分配。
        if (!domain.attach(*buffer)) {
            for (uint64_t i = 0; i < bytes; i += 4096) {
                mm::unmapPageInAddressSpace(root, userAddress + i);
            }

            mm::releasePhysicalBlock(physical, order, task.id());
            mm::freeHeap(buffer);

            return nullptr;
        }

        // 同时返回 CPU 使用的用户虚拟地址与设备使用的物理地址，两者不能混用。
        info.virtualAddress = userAddress;
        info.deviceAddress  = physical;
        info.size           = bytes;

        return buffer;
    }

    /*
     * 原子认领关闭动作，撤销本缓冲区的用户映射并归还资源域引用；实际物理页由设备停稳后的后台路径释放。
     */
    void DmaBuffer::destroy() {
        // 关闭只执行一次；这里只撤销访问和归还引用，物理内存必须等设备确认停止后由资源域回收。
        if (__atomic_exchange_n(&closed_, true, __ATOMIC_ACQ_REL)) {
            return;
        }

        domain_->unmap(virtualAddress_, size_, mapped_, physicalAddress_);
        domain_->drop();
    }

}  // namespace device
