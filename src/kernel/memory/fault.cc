/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/fault.cc
 * 缺页异常分类、内存服务补页与不可恢复错误处理。
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/mm.h"
#include "kernel/task.h"

namespace cpu {

    /*
     * 处理 CPU 报告的缺页异常。
     * 先读取出错地址和访问方式；能交给内存服务补页的用户访问先尝试补页，无法恢复时再转成进程信号或内核错误。
     */
    void handlePageFault(cpu::Context* context) {
        uint64_t faultAddress;

        // CR2
        // 保存的是触发本次异常的线性地址，先取出它，再结合错误位判断属于哪类访问失败。
        asm volatile("movq %%cr2, %0" : "=r"(faultAddress));
        int reason              = 0;
        ::task::Thread* current = ::task::currentThread();
        const bool userFault =
            (context->cs & 3) == 3 ||
            (context->rip >= 0x10000 && context->rip < 0x800000000000ULL &&
             current && current->getType() == ::task::ThreadType::Sys);
        // 保留位错误意味着页表本身无效，不能按普通的用户缺页处理。
        if (!(context->errorCode & 8) && (context->cs & 3) &&
            mm::pagerBridge().resolve(faultAddress, context->errorCode,
                                      &reason)) {
            return;
        }

        // 区分地址不存在、权限不足以及文件末尾或读写错误；把硬件报告的读、写、执行方式一并记入信号信息。
        if (userFault && !(context->errorCode & 8)) {
            const bool bus = reason == -11 || reason == -18;
            task::raiseException(
                context, bus ? 7 : 11,
                bus ? (reason == -18 ? 2 : 3)
                    : ((context->errorCode & 1) || reason == -3 ? 2
                       : reason == -4                           ? 1
                                                                : 128),
                faultAddress, true,
                (context->errorCode & 16)  ? ::task::FaultAccess::Execute
                : (context->errorCode & 2) ? ::task::FaultAccess::Write
                                           : ::task::FaultAccess::Read);

            return;
        }

        // 内核堆或锁本身可能已经损坏，只使用有长度限制的紧急输出并停止系统，不能再尝试文件日志和普通进程清理。
        device::emergencyWrite("[MEMORY] page fault address=");
        device::emergencyHex(faultAddress);
        device::emergencyWrite(" error=");
        device::emergencyHex(context->errorCode);
        device::emergencyWrite(" rip=");
        device::emergencyHex(context->rip);
        device::emergencyWrite("\n");

        device::emergencyWrite(
            "[MEMORY] kernel page fault or corrupt page tables\n");
        panic(context);
    }

}  // namespace cpu
