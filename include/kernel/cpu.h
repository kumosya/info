#pragma once

#include <stdint.h>

namespace kernel {

struct Thread;
struct Process;
struct RunQueue;

struct CpuFeatures {
    bool longMode;
    bool noExecute;
    bool sse;
    bool sse2;
    bool apic;
    bool x2Apic;
    bool tsc;
    bool invariantTsc;
};

struct [[gnu::packed]] TaskStateSegment {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t ioMapBase;
};

struct CpuLocalData {
    CpuLocalData* self;
    uint32_t cpuId;
    uint32_t apicId;
    Thread* currentThread;
    Process* currentProcess;
    RunQueue* runQueue;
    Thread* idleThread;
    void* kernelStack;
    TaskStateSegment* tss;
    void* idt;
    bool rescheduleRequested;
    uint64_t tickCount;
    uint64_t syscallUserStack;
    void* architectureData;
    uint32_t preemptionCount;
};

void initCpu();
CpuLocalData* prepareCpuLocalData(uint32_t cpuId, uint32_t apicId);
void initApplicationProcessorCpu(CpuLocalData* cpu);
const CpuFeatures& getCpuFeatures();
CpuLocalData* getCurrentCpu();
uint32_t getCurrentCpuId();
void setKernelStack(void* stackTop);

uint64_t readMsr(uint32_t msr);
void writeMsr(uint32_t msr, uint64_t value);

void initLocalApic();
void configureLocalApicTimer(uint8_t vector, uint32_t initialCount);
void sendEndOfInterrupt();
void sendInterProcessorInterrupt(uint32_t apicId, uint8_t vector);
void sendInitInterProcessorInterrupt(uint32_t apicId);
void sendStartupInterProcessorInterrupt(uint32_t apicId, uint8_t vector);

uint64_t disableInterrupts();
void restoreInterrupts(uint64_t flags);
void enableInterrupts();
void disablePreemption();
void enablePreemption();
bool isPreemptionEnabled();
[[noreturn]] void haltCpu();

}  // namespace kernel
