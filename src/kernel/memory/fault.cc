#include "kernel/cpu.h"
#include "kernel/interrupt.h"
#include "kernel/serial.h"
#include "kernel/task.h"
#include "kernel/mm.h"

namespace kernel::interrupt {

void handlePageFault(interrupt::Context* context) {
    uint64_t faultAddress;
    asm volatile("movq %%cr2, %0" : "=r"(faultAddress));
    int reason = 0;
    Thread* current = Thread::getCurrent();
    const bool userFault = (context->cs & 3) == 3 || (context->rip >= 0x10000 && context->rip < 0x800000000000ULL &&
        current && current->getType() == ThreadType::Sys);
    // A reserved-bit fault indicates invalid page tables, never a user mapping miss.
    if (!(context->errorCode & 8) && (context->cs & 3) &&
        memory::PagerBridge::resolve(faultAddress, context->errorCode, &reason)) return;
    if (userFault && !(context->errorCode & 8)) {
        const bool bus = reason == -11 || reason == -18;
        signal::raiseException(context, bus ? 7 : 11,
            bus ? (reason == -18 ? 2 : 3) : ((context->errorCode & 1) || reason == -3 ? 2 : reason == -4 ? 1 : 128),
            faultAddress, true, (context->errorCode & 16) ? FaultAccess::Execute :
                (context->errorCode & 2) ? FaultAccess::Write : FaultAccess::Read);
        return;
    }
    serial::emergencyWrite("[MEMORY] page fault address=");
    serial::emergencyHex(faultAddress);
    serial::emergencyWrite(" error=");
    serial::emergencyHex(context->errorCode);
    serial::emergencyWrite(" rip=");
    serial::emergencyHex(context->rip);
    serial::emergencyWrite("\n");

    serial::emergencyWrite("[MEMORY] kernel page fault or corrupt page tables\n");
    panic(context);
}

}  // namespace kernel::interrupt
