/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/object/mapping.cc
 * 建立内核堆的固定物理别名，并初始化页面内容。
 */

#include "kernel/mm.h"
#include "kernel/page.h"

namespace mm {

    // 内核堆使用独立的固定地址区间，以 heapBase
    // 加物理地址定位页面，无需再分配虚拟地址。
    constexpr uint64_t heapBase = 0xffff804000000000ULL,
                       heapSize = 64ULL * 1024 * 1024 * 1024;

    /*
     * 为连续物理页建立内核可用的访问地址。
     * 先核对范围，再补齐直接映射，使调用者能通过普通指针访问这些页。
     */
    void* mapKernelPages(uint64_t physical, uint32_t order) {
        if (!physical || order > maximumBuddyOrder || physical >= heapSize ||
            (4096ULL << order) > heapSize - physical) {
            return nullptr;
        }

        const uint64_t bytes = 4096ULL << order;

        // 逐页检查固定别名：已有映射必须指向同一物理页，缺失时才补建，不能覆盖别的用途。
        for (uint64_t offset = 0; offset < bytes; offset += 4096) {
            const uint64_t address  = heapBase + physical + offset;
            const uint64_t existing = mm::getPhysicalAddress(address);

            if (existing == physical + offset) {
                continue;
            }

            // 失败回滚不删除原有别名；本次可能留下新别名，但页块仍归调用者管理，下次使用会重新核对物理地址。
            if (existing || !mm::mapPage(address, physical + offset,
                                         pageWritable | pageNoExecute)) {
                return nullptr;
            }
        }

        // 所有页都具备正确映射后，才把连续的内核访问地址交给调用者。
        return reinterpret_cast<void*>(heapBase + physical);
    }

    /*
     * 把指定物理页范围清零。
     * 使用内核的直接映射地址写入，避免新分配对象读到上一位使用者留下的数据。
     */
    void clearPhysicalPages(uint64_t physical, uint64_t bytes) {
        uint8_t* output =
            reinterpret_cast<uint8_t*>(0xffff800000000000ULL + physical);

        for (uint64_t i = 0; i < bytes; ++i) {
            output[i] = 0;
        }
    }

    /*
     * 将内核直接映射指针换算成物理地址。
     * 这里只做地址换算，不分配页面，也不改变页面归属。
     */
    uint64_t physicalFromKernelPointer(const void* address) {
        const uint64_t value = reinterpret_cast<uint64_t>(address);

        return value >= heapBase && value - heapBase < heapSize
                   ? value - heapBase
                   : 0;
    }

}  // namespace mm
