#include "kernel/cpu.h"
#include "kernel/interrupt.h"
#include "kernel/serial.h"
#include "kernel/thread.h"

namespace kernel {

void handlePageFault(InterruptContext* context) {
    uint64_t faultAddress;
    asm volatile("movq %%cr2, %0" : "=r"(faultAddress));
    serialWrite("[MEMORY] page fault address=");
    serialWriteHex(faultAddress);
    serialWrite(" error=");
    serialWriteHex(context->errorCode);
    serialWrite(" rip=");
    serialWriteHex(context->rip);
    serialWrite("\n");

    if ((context->cs & 3) != 0) {
        serialWrite("[MEMORY] terminating user thread after page fault\n");
        terminateCurrentThreadFromException(-14);
    } else {
        serialWrite("[MEMORY] kernel page fault\n");
    }
    haltCpu();
}

}  // namespace kernel
