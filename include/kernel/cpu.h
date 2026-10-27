#pragma once

#define KERNEL_CPU_STACK_OFFSET 48
#define KERNEL_CPU_USER_STACK_OFFSET 88

#ifndef __ASSEMBLER__

#include <cstdint>

namespace kernel::task {
class Task;
class Thread;
class RunQueue;
}  // namespace kernel::task

namespace kernel::cpu {

class CpuFeatures {
   public:
    constexpr CpuFeatures()
        : longMode(false),
          noExecute(false),
          sse(false),
          sse2(false),
          apic(false),
          x2Apic(false),
          tsc(false),
          invariantTsc(false) {}
    void detect();

    bool hasLongMode() const { return longMode; }
    bool hasNoExecute() const { return noExecute; }
    bool hasSse() const { return sse; }
    bool hasSse2() const { return sse2; }
    bool hasApic() const { return apic; }
    bool hasX2Apic() const { return x2Apic; }
    bool hasTsc() const { return tsc; }
    bool hasInvariantTsc() const { return invariantTsc; }

   private:
    bool longMode;
    bool noExecute;
    bool sse;
    bool sse2;
    bool apic;
    bool x2Apic;
    bool tsc;
    bool invariantTsc;
};

struct [[gnu::packed]] TssTable {
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

struct [[gnu::packed]] DescriptorTablePointer {
    uint16_t limit;
    uint64_t base;
};

class CpuArchData {
   public:
    TssTable& getTss() { return tss; }
    uint64_t* getGdt() { return gdt; }

   private:
    TssTable tss;
    alignas(16) uint64_t gdt[7];
};

class CpuLocalData {
   public:
    constexpr CpuLocalData()
        : self_(this),
          cpuId(0),
          apicId(0),
          currentThread(nullptr),
          currentTask(nullptr),
          runQueue(nullptr),
          idleThread(nullptr),
          kernelStack(nullptr),
          tss(nullptr),
          idt(nullptr),
          rescheduleRequested(false),
          tickCount(0),
          syscallUserStack(0),
          archData(nullptr),
          preemptionCount(0) {}
    static CpuLocalData* prepare(uint32_t cpuId, uint32_t apicId);
    void disablePreemption();
    void enablePreemption();
    bool isPreemptionEnabled() const;
    void initApplicationTaskorCpu();
    void initDescriptorTables(CpuArchData* arch);

    void setCpuId(uint32_t id) { cpuId = id; }
    void setApicId(uint32_t id) { apicId = id; }
    void setKernelStack(void* stackTop);
    void setCurrentThread(kernel::task::Thread* thread) {
        __atomic_store_n(&currentThread, thread, __ATOMIC_RELEASE);
    }
    void setCurrentTask(kernel::task::Task* task) { currentTask = task; }
    void setRunQueue(kernel::task::RunQueue* queue) { runQueue = queue; }
    void setIdleThread(kernel::task::Thread* thread) { idleThread = thread; }
    void setRescheduleRequested(bool requested) {
        rescheduleRequested = requested;
    }
    void addTickCount(uint64_t count) { tickCount += count; }
    void addPreemptionCount(uint32_t count) { preemptionCount += count; }
    void setIdt(void* idtPointer) { idt = idtPointer; }

    uint32_t getCpuId() const { return cpuId; }
    uint32_t getApicId() const { return apicId; }
    kernel::task::Thread* getCurrentThread() const {
        return __atomic_load_n(&currentThread, __ATOMIC_ACQUIRE);
    }
    kernel::task::Task* getCurrentTask() const { return currentTask; }
    kernel::task::RunQueue* getRunQueue() const { return runQueue; }
    kernel::task::Thread* getIdleThread() const { return idleThread; }
    void* getKernelStack() const { return kernelStack; }
    void* getIdt() const { return idt; }
    bool isRescheduleRequested() const { return rescheduleRequested; }
    uint32_t getPreemptionCount() const { return preemptionCount; }
    static constexpr bool hasAssemblyLayout() {
        return __builtin_offsetof(CpuLocalData, self_) == 0 &&
               __builtin_offsetof(CpuLocalData, kernelStack) ==
                   KERNEL_CPU_STACK_OFFSET &&
               __builtin_offsetof(CpuLocalData, syscallUserStack) ==
                   KERNEL_CPU_USER_STACK_OFFSET;
    }

   private:
    CpuLocalData* self_;
    uint32_t cpuId;
    uint32_t apicId;
    kernel::task::Thread* currentThread;
    kernel::task::Task* currentTask;
    kernel::task::RunQueue* runQueue;
    kernel::task::Thread* idleThread;
    void* kernelStack;
    TssTable* tss;
    void* idt;
    bool rescheduleRequested;
    uint64_t tickCount;
    uint64_t syscallUserStack;
    void* archData;
    uint32_t preemptionCount;
};

class PreemptionGuard {
   public:
    explicit PreemptionGuard(CpuLocalData& owner) : owner_(owner) {
        owner_.disablePreemption();
    }
    PreemptionGuard();
    ~PreemptionGuard() { owner_.enablePreemption(); }
    PreemptionGuard(const PreemptionGuard&)            = delete;
    PreemptionGuard& operator=(const PreemptionGuard&) = delete;

   private:
    CpuLocalData& owner_;
};

void init();

const CpuFeatures& getCpuFeatures();
CpuLocalData* getCurrent();
uint32_t getCurrentId();

uint64_t readMsr(uint32_t msr);
void writeMsr(uint32_t msr, uint64_t value);

void disablePreemption();
void enablePreemption();
bool isPreemptionEnabled();
[[noreturn]] void halt();

void initLocalApic();
void initTimer(uint8_t vector, uint32_t initialCount);

void sendEndOfInterrupt();
void stopOtherCpus();
void sendInterTaskorInterrupt(uint32_t apicId, uint8_t vector);
void sendInitInterTaskorInterrupt(uint32_t apicId);
void sendStartupInterTaskorInterrupt(uint32_t apicId, uint8_t vector);

uint64_t disableInterrupts();
void restoreInterrupts(uint64_t flags);
void enableInterrupts();

}  // namespace kernel::cpu

#endif  // __ASSEMBLER__
