#pragma once

#include <stdint.h>

namespace kernel {

struct BootInfo;

struct InterruptContext {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t errorCode;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

using InterruptHandler = void (*)(InterruptContext* context);

void initIdt();
void loadIdt();
void initInterruptControllers(const BootInfo* bootInfo);
bool configureIrq(uint8_t irq, uint8_t vector, uint32_t apicId);
void maskIrq(uint8_t irq);
bool registerInterruptHandler(uint8_t vector, InterruptHandler handler);
void unregisterInterruptHandler(uint8_t vector);
void handleException(InterruptContext* context);
void handlePageFault(InterruptContext* context);

extern "C" void interruptDispatch(InterruptContext* context);

}  // namespace kernel
