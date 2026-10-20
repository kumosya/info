#pragma once

#include <cstdint>

namespace kernel {

struct BootInfo;

namespace interrupt {
struct [[gnu::packed]] Context {
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
using Handler = void (*)(Context* context);

void initIdt();
void loadIdt();
void initControllers(const BootInfo* bootInfo);
bool setIrq(uint8_t irq, uint8_t vector, uint32_t apicId, bool levelTriggered = false);
void maskIrq(uint8_t irq);
bool registerHandler(uint8_t vector, Handler handler);
void unregisterHandler(uint8_t vector);
void handleException(Context* context);
void handlePageFault(Context* context);

}  // namespace interrupt
}  // namespace kernel

namespace kernel::interrupt {
extern "C" void interruptDispatch(Context* context);
}
