#include "kernel/thread.h"

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/scheduler.h"
#include "kernel/serial.h"
#include "kernel/syscall.h"
#include "kernel/time.h"

namespace {

constexpr uint64_t kernelStackSize = 32 * 1024;
kernel::Atomic<uint64_t> nextThreadId{1};

void clearThread(kernel::Thread* thread) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(thread);
    for (uint64_t index = 0; index < sizeof(kernel::Thread); ++index) {
        bytes[index] = 0;
    }
}

uint64_t* buildInitialStack(void* stackTop) {
    uint64_t* stack = static_cast<uint64_t*>(stackTop);
    *--stack        = 0;
    *--stack        = reinterpret_cast<uint64_t>(kernel::threadBootstrap);
    *--stack        = 0x202;
    for (uint32_t registerIndex = 0; registerIndex < 15; ++registerIndex) {
        *--stack = 0;
    }
    return stack;
}

void forkedUserEntry(void* argument) {
    auto* context = static_cast<kernel::SyscallContext*>(argument);
    kernel::Thread* thread = kernel::getCurrentThread();
    if (context == nullptr || thread == nullptr) kernel::haltCpu();
    thread->type = kernel::ThreadType::User;
    kernel::resumeUserContext(context);
}

}  // namespace

namespace kernel {

void initThreadSubsystem() {
    Thread* bootThread = static_cast<Thread*>(kernelAllocate(sizeof(Thread)));
    if (bootThread == nullptr) {
        serialWrite("[THREAD] unable to allocate bootstrap thread\n");
        haltCpu();
    }
    clearThread(bootThread);
    bootThread->id    = nextThreadId.fetchAdd(1, kernel::MemoryOrder::Relaxed);
    bootThread->state = ThreadState::Running;
    bootThread->type  = ThreadType::Kernel;
    bootThread->cpuAffinity = ~0ULL;
    bootThread->weight      = 1024;
    bootThread->timeSlice   = millisecondsToNanoseconds(4);
    bootThread->cpuId       = getCurrentCpuId();
    uint64_t stackPointer;
    asm volatile("movq %%rsp, %0" : "=r"(stackPointer));
    bootThread->kernelStackTop = reinterpret_cast<void*>(stackPointer);

    CpuLocalData* cpu   = getCurrentCpu();
    cpu->currentThread  = bootThread;
    cpu->currentProcess = nullptr;
    serialWrite("[THREAD] bootstrap thread initialized\n");
}

Thread* createThread(Process* process, ThreadEntry entryPoint, void* argument,
                     ThreadType type, bool makeRunnable) {
    if (entryPoint == nullptr) {
        return nullptr;
    }
    Thread* thread = static_cast<Thread*>(kernelAllocate(sizeof(Thread)));
    uint8_t* stack = static_cast<uint8_t*>(kernelAllocate(kernelStackSize));
    if (thread == nullptr || stack == nullptr) {
        return nullptr;
    }
    clearThread(thread);
    thread->id             = nextThreadId.fetchAdd(1, MemoryOrder::Relaxed);
    thread->state          = ThreadState::Created;
    thread->type           = type;
    thread->kernelStack    = stack;
    thread->kernelStackTop = stack + kernelStackSize;
    thread->context.stackPointer =
        reinterpret_cast<uint64_t>(buildInitialStack(thread->kernelStackTop));
    thread->context.rip    = reinterpret_cast<uint64_t>(threadBootstrap);
    thread->context.rflags = 0x202;
    thread->process        = process;
    thread->cpuId          = getCurrentCpuId();
    thread->cpuAffinity    = ~0ULL;
    thread->weight         = getSchedulerWeight(0);
    thread->timeSlice      = millisecondsToNanoseconds(4);
    thread->entryPoint     = entryPoint;
    thread->argument       = argument;
    thread->state          = ThreadState::Ready;
    if (process != nullptr && !process->attachThread(thread)) {
        thread->state = ThreadState::Terminated;
        return nullptr;
    }
    if (makeRunnable && isSchedulerInitialized()) {
        enqueueThread(thread);
    }
    return thread;
}

Thread* createUserThread(Process* process, uint64_t entryPoint,
                         uint64_t userStackPointer, uint64_t argument) {
    if (process == nullptr || entryPoint == 0 || userStackPointer == 0 ||
        entryPoint >= 0x0000800000000000ULL ||
        userStackPointer >= 0x0000800000000000ULL) {
        return nullptr;
    }
    Thread* thread = createThread(
        process, reinterpret_cast<ThreadEntry>(entryPoint),
        reinterpret_cast<void*>(userStackPointer), ThreadType::User);
    if (thread != nullptr) {
        thread->userStack = reinterpret_cast<void*>(userStackPointer);
        thread->userArgument = reinterpret_cast<void*>(argument);
    }
    return thread;
}

Thread* createForkedUserThread(Process* process,
                               const SyscallContext& context,
                               uint64_t signalMask) {
    if (process == nullptr || context.userRip >= 0x0000800000000000ULL ||
        context.userRsp >= 0x0000800000000000ULL) {
        return nullptr;
    }
    auto* childContext =
        static_cast<SyscallContext*>(kernelAllocate(sizeof(SyscallContext)));
    if (childContext == nullptr) return nullptr;
    *childContext = context;
    childContext->rax = 0;
    Thread* thread = createThread(process, forkedUserEntry, childContext,
                                  ThreadType::Kernel, false);
    if (thread != nullptr) {
        thread->userStack = reinterpret_cast<void*>(context.userRsp);
        thread->signalMask = signalMask;
        if (isSchedulerInitialized()) enqueueThread(thread);
    }
    return thread;
}

extern "C" void threadBootstrap() {
    enableInterrupts();
    Thread* thread = getCurrentThread();
    if (thread->type == ThreadType::User) {
        enterUserMode(reinterpret_cast<uint64_t>(thread->entryPoint),
                      reinterpret_cast<uint64_t>(thread->userStack),
                      reinterpret_cast<uint64_t>(thread->userArgument));
    }
    thread->entryPoint(thread->argument);
    exitThread(0);
}

[[noreturn]] void exitThread(int exitCode) {
    const uint64_t flags = disableInterrupts();
    Thread* thread       = getCurrentThread();
    thread->exitCode     = exitCode;
    thread->state        = ThreadState::Terminated;
    if (thread->process != nullptr) {
        thread->process->notifyThreadExit(thread);
    }
    requestReschedule();
    schedule();
    restoreInterrupts(flags);
    haltCpu();
}

void blockThread() {
    const uint64_t flags = disableInterrupts();
    Thread* thread       = getCurrentThread();
    if (__atomic_exchange_n(&thread->wakePending, false, __ATOMIC_ACQUIRE)) {
        restoreInterrupts(flags);
        return;
    }
    thread->state = ThreadState::Blocked;
    requestReschedule();
    schedule();
    restoreInterrupts(flags);
}

void wakeThread(Thread* thread) {
    if (thread == nullptr) {
        return;
    }
    if (thread->state == ThreadState::Running) {
        __atomic_store_n(&thread->wakePending, true, __ATOMIC_RELEASE);
        return;
    }
    if (thread->state == ThreadState::Stopped &&
        (thread->stateBeforeStop == ThreadState::Blocked ||
         thread->stateBeforeStop == ThreadState::Sleeping)) {
        thread->stateBeforeStop = ThreadState::Ready;
        return;
    }
    if (thread->state != ThreadState::Blocked &&
        thread->state != ThreadState::Sleeping) {
        return;
    }
    thread->state = ThreadState::Ready;
    enqueueThread(thread);
}

void sleepThread(uint64_t milliseconds) { sleepCurrentThread(milliseconds); }

void switchThread(Thread* previous, Thread* next) {
    switchThreadContext(&previous->context.stackPointer,
                        next->context.stackPointer);
}

Thread* getCurrentThread() { return getCurrentCpu()->currentThread; }

void terminateCurrentThreadFromException(int exitCode) {
    Thread* thread = getCurrentThread();
    if (thread == nullptr || thread->type != ThreadType::User) {
        haltCpu();
    }
    exitThread(exitCode);
}

}  // namespace kernel
