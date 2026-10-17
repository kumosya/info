#include "kernel/cpu.h"
#include "kernel/interrupt.h"
#include "kernel/serial.h"
#include "kernel/thread.h"

namespace kernel::interrupt {

void handlePageFault(interrupt::Context* context) {
    uint64_t faultAddress;
    asm volatile("movq %%cr2, %0" : "=r"(faultAddress));
    serial::serialWrite("[MEMORY] page fault address=");
    serial::serialWriteHex(faultAddress);
    serial::serialWrite(" error=");
    serial::serialWriteHex(context->errorCode);
    serial::serialWrite(" rip=");
    serial::serialWriteHex(context->rip);
    serial::serialWrite("\n");

    if ((context->cs & 3) != 0) {
        serial::serialWrite(
            "[MEMORY] terminating user thread after page fault\n");
        Thread::terminateCurrentFromException(-14);
    } else {
        serial::serialWrite("[MEMORY] kernel page fault\n");
    }
    cpu::halt();
}

}  // namespace kernel::interrupt
