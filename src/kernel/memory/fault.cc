/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/fault.cc
 * Page-fault classification, pager recovery, and fatal-fault reporting.
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/mm.h"
#include "kernel/task.h"

namespace kernel::cpu {

// Read CR2 before nested kernel work can overwrite it. Ask the user pager only
// for recoverable user faults; a successful resolution retries the same
// instruction. Unresolved attributable faults become signals, while kernel
// faults or reserved-bit corruption enter emergency diagnostics.
void handlePageFault(cpu::Context* context) {
    uint64_t faultAddress;
    asm volatile("movq %%cr2, %0" : "=r"(faultAddress));
    int reason                      = 0;
    ::kernel::task::Thread* current = ::kernel::task::currentThread();
    const bool userFault =
        (context->cs & 3) == 3 ||
        (context->rip >= 0x10000 && context->rip < 0x800000000000ULL &&
         current && current->getType() == ::kernel::task::ThreadType::Sys);
    // A reserved-bit fault indicates invalid page tables, never a user mapping
    // miss.
    if (!(context->errorCode & 8) && (context->cs & 3) &&
        mm::pagerBridge().resolve(faultAddress, context->errorCode, &reason))
        return;
    // Distinguish mapping/permission failures from file-backed EOF or I/O
    // errors. Record the access kind from hardware flags so SIGSEGV/SIGBUS
    // consumers see an instruction, write or read fault instead of a
    // fabricated exit code.
    if (userFault && !(context->errorCode & 8)) {
        const bool bus = reason == -11 || reason == -18;
        task::raiseException(
            context, bus ? 7 : 11,
            bus ? (reason == -18 ? 2 : 3)
                : ((context->errorCode & 1) || reason == -3 ? 2
                   : reason == -4                           ? 1
                                                            : 128),
            faultAddress, true,
            (context->errorCode & 16)  ? ::kernel::task::FaultAccess::Execute
            : (context->errorCode & 2) ? ::kernel::task::FaultAccess::Write
                                       : ::kernel::task::FaultAccess::Read);
        return;
    }
    // The normal heap, locks or service machinery may be the fault source. Use
    // bounded emergency output and panic rather than attempting filesystem
    // logging or ordinary process cleanup from a corrupt kernel context.
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

}  // namespace kernel::cpu
