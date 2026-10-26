#include "kernel/interrupt.h"
#include "kernel/task.h"
#include "kernel/cpu.h"
#include "kernel/serial.h"

namespace kernel::interrupt {
namespace {
int floatingCode(uint32_t pending) {
    // Fixed priority: invalid, divide by zero, overflow, underflow, precision, denormal.
    if (pending & 1) return 7;
    if (pending & 4) return 3;
    if (pending & 8) return 4;
    if (pending & 16) return 5;
    if (pending & 32) return 6;
    return 5;
}
}
bool dispatchUserException(Context* context) {
    switch (context->vector) {
        case 0: case 1: case 3: case 4: case 5: case 6: case 12: case 13: case 16: case 17: case 19: break;
        default: return false;
    }
    Thread* current = Thread::getCurrent();
    const bool serviceInstruction = context->rip >= 0x10000 && context->rip < 0x800000000000ULL &&
        current && current->getType() == ThreadType::Sys;
    if ((context->cs & 3) != 3 && !serviceInstruction) return false;
    uint32_t signo = 0; int code = 128;
    uint64_t address = 0, debug = 0; bool valid = false;
    switch (context->vector) {
        case 0: signo = 8; code = 1; address = context->rip; valid = true; break;
        case 1:
            asm volatile("movq %%dr6, %0" : "=r"(debug));
            asm volatile("movq %0, %%dr6" : : "r"(0xffff0ff0ULL));
            signo = 5; code = (debug & (1ULL << 14)) ? 2 : (debug & 15) ? 4 : 128;
            address = context->rip; valid = true; break;
        case 3: signo = 5; code = 1; address = context->rip; valid = true; break;
        case 4: signo = 8; code = 2; address = context->rip; valid = true; break;
        case 5: signo = 11; code = 3; break;
        case 6: signo = 4; code = 1; address = context->rip; valid = true; break;
        // TSS, #NM, CET and platform exceptions require an enabled architectural policy.
        // They cannot safely be attributed to the user merely by examining saved CS.
        // No LDT or non-present user segments are supported. #NP therefore
        // indicates descriptor/delivery corruption, not a recoverable user load.
        case 11: return false;
        case 12: if (context->errorCode) return false; signo = 11; break;
        case 13: if (context->errorCode & 1) return false; signo = 11; break;
        case 16: case 19: {
            Thread* thread = Thread::getCurrent();
            if (!thread) return false;
            thread->saveFloatingPointState(); const uint8_t* fp = thread->fpState();
            const uint32_t pending = context->vector == 16
                ? (*reinterpret_cast<const uint16_t*>(fp + 2) & ~*reinterpret_cast<const uint16_t*>(fp)) & 63
                : (*reinterpret_cast<const uint32_t*>(fp + 24) & ~(*reinterpret_cast<const uint32_t*>(fp + 24) >> 7)) & 63;
            if (!pending) return false;
            signo = 8; code = floatingCode(pending); address = context->rip; valid = true; break;
        }
        case 17: signo = 7; code = 1; break;
        default: return false;
    }
    signal::raiseException(context, signo, code, address, valid, FaultAccess::Unknown, debug);
    return true;
}
}
