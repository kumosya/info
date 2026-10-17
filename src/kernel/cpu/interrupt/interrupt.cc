#include "kernel/interrupt.h"

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "kernel/syscall.h"

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
kernel::interrupt::Handler handlers[256];
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

void printException(const kernel::interrupt::Context* context) {
    kernel::serial::serialWrite("[INTERRUPT] exception vector=");
    kernel::serial::serialWriteDecimal(context->vector);
    kernel::serial::serialWrite(" error=");
    kernel::serial::serialWriteHex(context->errorCode);
    kernel::serial::serialWrite(" rip=");
    kernel::serial::serialWriteHex(context->rip);
    kernel::serial::serialWrite("\n");
}

}  // namespace

namespace kernel::interrupt {

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
    cpu::getCurrent()->setIdt(bootstrapIdt);
    loadIdt();
    serial::serialWrite("[INTERRUPT] IDT initialized with 256 vectors\n");
}

void loadIdt() {
    CpuLocalData* cpu = cpu::getCurrent();
    if (cpu->getIdt() == nullptr) {
        IdtEntry* idt = static_cast<IdtEntry*>(
            kernel::heap::kernelAllocate(sizeof(bootstrapIdt)));
        if (idt == nullptr) {
            serial::serialWrite("[INTERRUPT] unable to allocate per-CPU IDT\n");
            cpu::halt();
        }
        for (uint32_t index = 0; index < 256; ++index) {
            idt[index] = bootstrapIdt[index];
        }
        cpu->setIdt(idt);
    }
    const IdtPointer pointer{sizeof(bootstrapIdt) - 1,
                             reinterpret_cast<uint64_t>(cpu->getIdt())};
    asm volatile("lidt %0" : : "m"(pointer));
}

bool registerHandler(uint8_t vector, Handler handler) {
    InterruptSpinLockGuard guard(handlerLock);
    if (handler == nullptr || handlers[vector] != nullptr) {
        return false;
    }
    __atomic_store_n(&handlers[vector], handler, __ATOMIC_RELEASE);
    return true;
}

void unregisterHandler(uint8_t vector) {
    InterruptSpinLockGuard guard(handlerLock);
    __atomic_store_n(&handlers[vector], nullptr, __ATOMIC_RELEASE);
}

void handleException(Context* context) {
    if (context->vector == 14) {
        handlePageFault(context);
        return;
    }
    printException(context);
    if (context->vector == 3 && (context->cs & 3) == 0) {
        return;
    }
    if ((context->cs & 3) != 0) {
        serial::serialWrite(
            "[INTERRUPT] terminating user thread after exception\n");
        Thread::terminateCurrentFromException(
            -static_cast<int>(context->vector));
    }
    serial::serialWrite("[INTERRUPT] unrecoverable kernel exception\n");
    cpu::halt();
}

}  // namespace kernel::interrupt

namespace kernel::interrupt {
extern "C" void interruptDispatch(Context* context) {
    if (context->vector == 0x80) {
        Task* caller = task::getCurrent();
        if (!caller || !caller->isSystemService() || (context->cs & 3)) {
            context->rax = static_cast<uint64_t>(SyscallError::PermissionDenied);
            return;
        }
        // This DPL-0 gate never uses SYSRET or swaps GS. Normal Ring 3 callers
        // keep the SYSCALL path and cannot enter this service-only gate.
        interrupt::enable();
        context->rax = syscall::dispatchSyscall(context->rax, context->rdi, context->rsi,
                                               context->rdx, context->r10, context->r8, context->r9);
        interrupt::disable();
        return;
    }
    if (context->vector < 32) {
        kernel::interrupt::handleException(context);
        return;
    }
    kernel::interrupt::Handler handler =
        __atomic_load_n(&handlers[context->vector], __ATOMIC_ACQUIRE);
    if (handler != nullptr) {
        handler(context);
    }
    if (context->vector != 0xff) {
        kernel::localapic::sendEndOfInterrupt();
    }
    if (kernel::schedule::isSchedulerInitialized() &&
        kernel::cpu::isPreemptionEnabled() &&
        kernel::cpu::getCurrent()->isRescheduleRequested()) {
        kernel::schedule::schedule();
    }
}

}  // namespace kernel::interrupt
