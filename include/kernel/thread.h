#pragma once

#include <stdint.h>

namespace kernel {

struct Process;
struct SyscallContext;

enum class ThreadState : uint8_t {
    Created,
    Ready,
    Running,
    Blocked,
    Sleeping,
    Stopped,
    Terminated
};

enum class ThreadType : uint8_t { Kernel, User };

using ThreadEntry = void (*)(void* argument);

struct CpuContext {
    uint64_t stackPointer;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr3;
    uint64_t fsBase;
    uint64_t gsBase;
};

struct Thread {
    uint64_t id;
    ThreadState state;
    ThreadType type;
    void* kernelStack;
    void* kernelStackTop;
    void* userStack;
    CpuContext context;
    Process* process;
    uint64_t cpuId;
    uint64_t cpuAffinity;
    uint64_t vruntime;
    uint64_t runtime;
    uint64_t timeSlice;
    uint64_t sliceRuntime;
    uint32_t weight;
    int32_t nice;
    ThreadEntry entryPoint;
    void* argument;
    void* userArgument;
    int exitCode;

    Thread* rbParent;
    Thread* rbLeft;
    Thread* rbRight;
    bool rbRed;
    bool inRunQueue;
    bool wakePending;
    ThreadState stateBeforeStop;
    Thread* waitNext;
    Thread* joinWaiters;
    Thread* joinWaitersTail;
    volatile uint32_t* waitAddress;
    uint64_t waitKey;
    uint64_t signalMask;
    uint64_t pendingSignals;
    Thread* processNext;
    uint64_t wakeDeadlineNanoseconds;
};

void initThreadSubsystem();
Thread* createThread(Process* process, ThreadEntry entryPoint, void* argument,
                     ThreadType type = ThreadType::Kernel,
                     bool makeRunnable = true);
Thread* createUserThread(Process* process, uint64_t entryPoint,
                         uint64_t userStackPointer,
                         uint64_t argument = 0);
Thread* createForkedUserThread(Process* process,
                               const SyscallContext& context,
                               uint64_t signalMask);
[[noreturn]] void exitThread(int exitCode);
void blockThread();
void wakeThread(Thread* thread);
void sleepThread(uint64_t milliseconds);
void switchThread(Thread* previous, Thread* next);
Thread* getCurrentThread();
void terminateCurrentThreadFromException(int exitCode);

extern "C" void threadBootstrap();
extern "C" [[noreturn]] void enterUserMode(uint64_t entryPoint,
                                           uint64_t stackPointer,
                                           uint64_t argument);
extern "C" [[noreturn]] void resumeUserContext(SyscallContext* context);
extern "C" void switchThreadContext(uint64_t* previousStackPointer,
                                    uint64_t nextStackPointer);

}  // namespace kernel
