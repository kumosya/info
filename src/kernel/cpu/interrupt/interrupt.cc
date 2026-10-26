#include "kernel/interrupt.h"

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"
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
    static constexpr const char* names[32] = {
        "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
        "#DF", "legacy coprocessor overrun", "#TS", "#NP", "#SS", "#GP", "#PF", "reserved 15",
        "#MF", "#AC", "#MC", "#XM", "#VE", "#CP", "reserved 22", "reserved 23",
        "reserved 24", "reserved 25", "reserved 26", "reserved 27", "#HV", "#VC", "#SX", "reserved 31"
    };
    kernel::serial::emergencyWrite("[INTERRUPT] ");
    kernel::serial::emergencyWrite(context->vector < 32 ? names[context->vector] : "unexpected vector");
    kernel::serial::emergencyWrite(" exception vector=");
    kernel::serial::emergencyHex(context->vector);
    kernel::serial::emergencyWrite(" error=");
    kernel::serial::emergencyHex(context->errorCode);
    kernel::serial::emergencyWrite(" rip=");
    kernel::serial::emergencyHex(context->rip);
    kernel::serial::emergencyWrite(" rsp=");
    kernel::serial::emergencyHex(context->rsp);
    kernel::serial::emergencyWrite("\n");
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
        const uint8_t privilege = vector == 3 ? 3 : 0;
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
    if (context->vector == 2 || context->vector == 8 || context->vector == 18) panic(context);
    if (context->vector == 14) {
        handlePageFault(context);
        return;
    }
    if (dispatchUserException(context)) return;
    panic(context);
}

[[noreturn]] void panic(Context* context) {
    interrupt::disable();
    static bool panicking = false;
    if (__atomic_exchange_n(&panicking, true, __ATOMIC_ACQ_REL)) cpu::halt();
    localapic::stopOtherCpus();
    printException(context);
    serial::emergencyWrite("[INTERRUPT] unrecoverable kernel exception\n");
    cpu::halt();
}

}  // namespace kernel::interrupt

namespace kernel::interrupt {
extern "C" void interruptDispatch(Context* context) {
    if (context->vector < 32) {
        kernel::interrupt::handleException(context);
        kernel::signal::returnToUser(context);
        return;
    }
    kernel::signal::captureUserContext(context);
    kernel::interrupt::Handler handler =
        __atomic_load_n(&handlers[context->vector], __ATOMIC_ACQUIRE);
    if (handler != nullptr) {
        handler(context);
    } else if (context->vector != 0xff) {
        static uint64_t reported[4]{};
        const auto index = context->vector / 64;
        const auto bit = 1ULL << (context->vector % 64);
        if (!(__atomic_fetch_or(&reported[index], bit, __ATOMIC_RELAXED) & bit)) {
            serial::serialWrite("[INTERRUPT] unregistered vector acknowledged: ");
            serial::serialWriteDecimal(context->vector); serial::serialWrite("\n");
        }
    }
    if (context->vector != 0xff) {
        kernel::localapic::sendEndOfInterrupt();
    }
    if (kernel::schedule::isSchedulerInitialized() &&
        kernel::cpu::isPreemptionEnabled() &&
        kernel::cpu::getCurrent()->isRescheduleRequested()) {
        kernel::schedule::schedule();
    }
    kernel::signal::returnToUser(context);
}

}  // namespace kernel::interrupt
