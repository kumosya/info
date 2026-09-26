#include "kernel/interrupt.h"

#include "kernel/cpu.h"
#include "kernel/heap.h"
#include "kernel/scheduler.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"

namespace {

struct [[gnu::packed]] IdtEntry {
    uint16_t offsetLow;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offsetMiddle;
    uint32_t offsetHigh;
    uint32_t reserved;
};

struct [[gnu::packed]] IdtPointer {
    uint16_t limit;
    uint64_t base;
};

alignas(16) IdtEntry bootstrapIdt[256];
kernel::InterruptHandler handlers[256];
kernel::SpinLock handlerLock;
extern "C" void* isrStubTable[];

void writePort(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void maskLegacyPic() {
    writePort(0x21, 0xff);
    writePort(0xa1, 0xff);
}

void setIdtEntry(uint8_t vector, void* entry, uint8_t ist, uint8_t privilege) {
    const uint64_t address  = reinterpret_cast<uint64_t>(entry);
    IdtEntry& descriptor    = bootstrapIdt[vector];
    descriptor.offsetLow    = address & 0xffff;
    descriptor.selector     = 0x08;
    descriptor.ist          = ist & 0x07;
    descriptor.attributes   = 0x8e | ((privilege & 0x03) << 5);
    descriptor.offsetMiddle = (address >> 16) & 0xffff;
    descriptor.offsetHigh   = address >> 32;
    descriptor.reserved     = 0;
}

void printException(const kernel::InterruptContext* context) {
    kernel::serialWrite("[INTERRUPT] exception vector=");
    kernel::serialWriteDecimal(context->vector);
    kernel::serialWrite(" error=");
    kernel::serialWriteHex(context->errorCode);
    kernel::serialWrite(" rip=");
    kernel::serialWriteHex(context->rip);
    kernel::serialWrite("\n");
}

}  // namespace

namespace kernel {

void initIdt() {
    maskLegacyPic();
    for (uint16_t vector = 0; vector < 256; ++vector) {
        uint8_t ist = 0;
        if (vector == 8) {
            ist = 1;
        } else if (vector == 2) {
            ist = 2;
        } else if (vector == 18) {
            ist = 3;
        }
        const uint8_t privilege = (vector == 3 || vector == 0x80) ? 3 : 0;
        setIdtEntry(static_cast<uint8_t>(vector), isrStubTable[vector], ist,
                    privilege);
        handlers[vector] = nullptr;
    }
    getCurrentCpu()->idt = bootstrapIdt;
    loadIdt();
    serialWrite("[INTERRUPT] IDT initialized with 256 vectors\n");
}

void loadIdt() {
    CpuLocalData* cpu = getCurrentCpu();
    if (cpu->idt == nullptr) {
        IdtEntry* idt =
            static_cast<IdtEntry*>(kernelAllocate(sizeof(bootstrapIdt)));
        if (idt == nullptr) {
            serialWrite("[INTERRUPT] unable to allocate per-CPU IDT\n");
            haltCpu();
        }
        for (uint32_t index = 0; index < 256; ++index) {
            idt[index] = bootstrapIdt[index];
        }
        cpu->idt = idt;
    }
    const IdtPointer pointer{sizeof(bootstrapIdt) - 1,
                             reinterpret_cast<uint64_t>(cpu->idt)};
    asm volatile("lidt %0" : : "m"(pointer));
}

bool registerInterruptHandler(uint8_t vector, InterruptHandler handler) {
    InterruptSpinLockGuard guard(handlerLock);
    if (handler == nullptr || handlers[vector] != nullptr) {
        return false;
    }
    __atomic_store_n(&handlers[vector], handler, __ATOMIC_RELEASE);
    return true;
}

void unregisterInterruptHandler(uint8_t vector) {
    InterruptSpinLockGuard guard(handlerLock);
    __atomic_store_n(&handlers[vector], nullptr, __ATOMIC_RELEASE);
}

void handleException(InterruptContext* context) {
    if (context->vector == 14) {
        handlePageFault(context);
        return;
    }
    printException(context);
    if (context->vector == 3 && (context->cs & 3) == 0) {
        return;
    }
    if ((context->cs & 3) != 0) {
        serialWrite("[INTERRUPT] terminating user thread after exception\n");
        terminateCurrentThreadFromException(-static_cast<int>(context->vector));
    }
    serialWrite("[INTERRUPT] unrecoverable kernel exception\n");
    haltCpu();
}

extern "C" void interruptDispatch(InterruptContext* context) {
    if (context->vector < 32) {
        handleException(context);
        return;
    }
    InterruptHandler handler =
        __atomic_load_n(&handlers[context->vector], __ATOMIC_ACQUIRE);
    if (handler != nullptr) {
        handler(context);
    }
    if (context->vector != 0xff) {
        sendEndOfInterrupt();
    }
    if (isSchedulerInitialized() && isPreemptionEnabled() &&
        getCurrentCpu()->rescheduleRequested) {
        schedule();
    }
}

}  // namespace kernel
