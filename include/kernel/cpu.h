/*
 * CPU 状态与架构操作。
 */

#pragma once

#define KERNEL_CPU_STACK_OFFSET 48
#define KERNEL_CPU_USER_STACK_OFFSET 88

#ifndef __ASSEMBLER__

#include <cstdint>

namespace task {
    class Task;
    class Thread;
    class RunQueue;
}  // namespace task

namespace cpu {

    /* 记录 CPU 支持的硬件功能，供启动和指令选择判断。 */
    class Features {
        public:
            constexpr Features()
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

    /* 保存当前 CPU 的架构相关状态，例如描述符和特权栈。 */
    class ArchData {
        public:
            TssTable& getTss() { return tss; }
            uint64_t* getGdt() { return gdt; }

        private:
            TssTable tss;
            alignas(16) uint64_t gdt[7];
    };

    /* 保存每个 CPU 独立的当前线程、任务和调度信息。 */
    class LocalData {
        public:
            constexpr LocalData()
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
            static LocalData* prepare(uint32_t cpuId, uint32_t apicId);
            void disablePreemption();
            void enablePreemption();
            bool isPreemptionEnabled() const;
            void initApplicationTaskorCpu();
            void initDescriptorTables(ArchData* arch);

            void setCpuId(uint32_t id) { cpuId = id; }
            void setApicId(uint32_t id) { apicId = id; }
            void setKernelStack(void* stackTop);
            void setCurrentThread(task::Thread* thread) {
                __atomic_store_n(&currentThread, thread, __ATOMIC_RELEASE);
            }
            void setCurrentTask(task::Task* task) { currentTask = task; }
            void setRunQueue(task::RunQueue* queue) { runQueue = queue; }
            void setIdleThread(task::Thread* thread) { idleThread = thread; }
            void setRescheduleRequested(bool requested) {
                rescheduleRequested = requested;
            }
            void addTickCount(uint64_t count) { tickCount += count; }
            void addPreemptionCount(uint32_t count) {
                preemptionCount += count;
            }
            void setIdt(void* idtPointer) { idt = idtPointer; }

            uint32_t getCpuId() const { return cpuId; }
            uint32_t getApicId() const { return apicId; }
            task::Thread* getCurrentThread() const {
                return __atomic_load_n(&currentThread, __ATOMIC_ACQUIRE);
            }
            task::Task* getCurrentTask() const { return currentTask; }
            task::RunQueue* getRunQueue() const { return runQueue; }
            task::Thread* getIdleThread() const { return idleThread; }
            void* getKernelStack() const { return kernelStack; }
            void* getIdt() const { return idt; }
            bool isRescheduleRequested() const { return rescheduleRequested; }
            uint32_t getPreemptionCount() const { return preemptionCount; }
            static constexpr bool hasAssemblyLayout() {
                return __builtin_offsetof(LocalData, self_) == 0 &&
                       __builtin_offsetof(LocalData, kernelStack) ==
                           KERNEL_CPU_STACK_OFFSET &&
                       __builtin_offsetof(LocalData, syscallUserStack) ==
                           KERNEL_CPU_USER_STACK_OFFSET;
            }

        private:
            LocalData* self_;
            uint32_t cpuId;
            uint32_t apicId;
            task::Thread* currentThread;
            task::Task* currentTask;
            task::RunQueue* runQueue;
            task::Thread* idleThread;
            void* kernelStack;
            TssTable* tss;
            void* idt;
            bool rescheduleRequested;
            uint64_t tickCount;
            uint64_t syscallUserStack;
            void* archData;
            uint32_t preemptionCount;
    };

    /* 在作用域内限制调度抢占，离开作用域时自动恢复原状态。 */
    class PreemptionGuard {
        public:
            explicit PreemptionGuard(LocalData& owner) : owner_(owner) {
                owner_.disablePreemption();
            }
            PreemptionGuard();
            ~PreemptionGuard() { owner_.enablePreemption(); }
            PreemptionGuard(const PreemptionGuard&)            = delete;
            PreemptionGuard& operator=(const PreemptionGuard&) = delete;

        private:
            LocalData& owner_;
    };

    void init();
    bool hasWriteCombining();

    const Features& getFeatures();
    LocalData* getCurrent();
    uint32_t getCurrentId();

    uint64_t readMsr(uint32_t msr);
    void writeMsr(uint32_t msr, uint64_t value);

    void disablePreemption();
    void enablePreemption();
    bool isPreemptionEnabled();
    [[noreturn]] void halt();

    void initLocalApic();
    void initTimer(uint8_t vector, uint32_t intervalMicroseconds);

    void sendEndOfInterrupt();
    void stopOtherCpus();
    void sendInterTaskorInterrupt(uint32_t apicId, uint8_t vector);
    void sendInitInterTaskorInterrupt(uint32_t apicId);
    void sendStartupInterTaskorInterrupt(uint32_t apicId, uint8_t vector);

    uint64_t disableInterrupts();
    void restoreInterrupts(uint64_t flags);
    void enableInterrupts();

}  // namespace cpu

#endif  // __ASSEMBLER__
