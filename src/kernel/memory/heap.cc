/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/heap.cc
 * 内核堆分配：小对象复用缓存，大对象使用连续页块。
 */

#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/page.h"

namespace mm {

    constexpr uint64_t allocatedMagic = 0x48454150414c4c4fULL;
    constexpr uint64_t freedMagic     = 0x4845415046524545ULL;
    // 大小档位必须包含内部 Header，不能只按调用者需要的有效数据长度选择。
    static constexpr uint32_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048};
    static mm::PerCpuObjectCache caches[7];

    // 每次分配前放置释放所需的来源记录，同时保持返回数据按 16
    // 字节对齐；UINT16_MAX 表示整块物理页分配。
    struct alignas(16) Header {
            uint64_t magic;
            uint32_t order;
            uint16_t bucket;
            uint16_t reserved;
    };

    /*
     * 初始化内核的小对象缓存，按常用大小建立分配档位。
     * 此后小块申请可以复用已准备好的空间。
     */
    void initHeap() {
        for (uint32_t i = 0; i < 7; ++i) {
            caches[i].init(sizes[i]);
        }

        device::serialWrite(
            "[MEMORY] slab, per-CPU magazines and large-block buddy heap "
            "initialized\n");
    }

    /*
     * 申请内核堆内存。
     * 小对象从对应缓存取得，大对象申请连续物理页；在返回地址前保存分配来源，方便释放时找到正确的归还路径。
     */
    void* allocateHeap(size_t size) {
        // 先排除零长度和过大申请，保证后面加上管理头不会溢出。
        if (!size || size > (1ULL << 30) - sizeof(Header)) {
            return nullptr;
        }

        const uint64_t required = size + sizeof(Header);

        // 从最小档位开始寻找能容纳“管理头加有效数据”的缓存，避免小申请占用整页。
        for (uint16_t i = 0; i < 7; ++i) {
            if (required <= sizes[i]) {
                Header* header = static_cast<Header*>(caches[i].allocate());

                if (!header) {
                    return nullptr;
                }

                // 缓存返回的空间前部留给管理头；调用者拿到 header
                // 后一个位置，只能使用自己的数据区。
                header->magic  = allocatedMagic;
                header->bucket = i;
                header->order  = 0;

                return header + 1;
            }
        }

        // 把分配头和有效数据一起向上取整为物理页；释放时必须使用保存的原始块大小。
        uint32_t order = 0;

        while ((4096ULL << order) < required) {
            ++order;
        }

        const uint64_t physical = mm::allocatePhysicalBlock(order);

        if (!physical) {
            return nullptr;
        }

        Header* header =
            static_cast<Header*>(mm::mapKernelPages(physical, order));

        // 物理页虽已申请成功，但内核映射失败仍要归还它，不能只返回空指针。
        if (!header) {
            mm::releasePhysicalBlock(physical, order);

            return nullptr;
        }

        header->magic = allocatedMagic;

        // 这个特殊档位标识整块页面分配，释放时据此绕过小对象缓存。
        header->bucket = UINT16_MAX;
        header->order  = order;

        return header + 1;
    }

    /*
     * 归还一次堆分配。
     * 先读回对象前面的分配头，再将小对象还给缓存，或按原来的页块大小释放大对象；空指针直接忽略。
     */
    void freeHeap(void* object) {
        if (!object || (reinterpret_cast<uint64_t>(object) & 15)) {
            return;
        }

        // 用户指针前面紧邻分配头，先还原它，再确认这段地址确实属于内核堆。
        Header* header          = static_cast<Header*>(object) - 1;
        const uint64_t address  = reinterpret_cast<uint64_t>(header);
        const uint64_t physical = mm::physicalFromKernelPointer(header);

        if (!physical) {
            return;
        }

        // 核对底层页仍处于已分配状态且没有归属用户任务，避免把任意用户页面交回内核缓存。
        mm::PhysicalPageInfo info{};

        if (!mm::queryPhysicalPage(physical, info) ||
            info.getState() != mm::FrameState::Allocated || info.getOwner()) {
            return;
        }

        uint64_t expected = allocatedMagic;
        // 只有成功认领分配头的释放者可以归还对象；之后这段内存可能立即被缓存或空闲链表覆盖。
        if (!__atomic_compare_exchange_n(&header->magic, &expected, freedMagic,
                                         false, __ATOMIC_ACQ_REL,
                                         __ATOMIC_ACQUIRE)) {
            return;
        }

        // 成功认领后按原分配来源归还；整页块还须核对对齐和阶数，不能把错误大小还给分配器。
        if (header->bucket < 7) {
            caches[header->bucket].release(header);
        } else if (!(address & 4095) && header->bucket == UINT16_MAX &&
                   header->order == info.getOrder()) {
            mm::releasePhysicalBlock(physical, header->order);
        }
    }

    /*
     * 清空各 CPU 暂存的空闲对象，再释放完全空闲的缓存页。
     * 返回本次实际归还的内存量。
     */
    uint64_t reclaimCaches() {
        uint64_t pages = 0;

        for (mm::PerCpuObjectCache& cache : caches) {
            pages += cache.drain();
        }

        return pages * 4096;
    }

}  // namespace mm
