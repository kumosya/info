#include "kernel/thread.h"

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "kernel/ipc.h"

static_assert(sizeof(kernel::CpuContext) == 21 * sizeof(uint64_t));

namespace {

constexpr uint64_t kernelStackSize = 64 * 1024;
kernel::Atomic<uint64_t> nextThreadId{1};

}  // namespace

namespace kernel {

void Thread::init() {
    Thread* bootThread =
        static_cast<Thread*>(heap::kernelAllocate(sizeof(Thread)));
    if (bootThread == nullptr) {
        serial::serialWrite("[THREAD] unable to allocate bootstrap thread\n");
        cpu::halt();
    }
    Thread::clearThread(bootThread);
    bootThread->setId(nextThreadId.fetchAdd(1, kernel::MemoryOrder::Relaxed));
    bootThread->setState(ThreadState::Running);
    bootThread->setType(ThreadType::Kernel);
    bootThread->setCpuAffinity(~0ULL);
    bootThread->setWeight(1024);
    bootThread->setTimeSlice(time::milliseconds2Nanoseconds(4));
    bootThread->setCpuId(cpu::getCurrentId());
    uint64_t stackPointer;
    asm volatile("movq %%rsp, %0" : "=r"(stackPointer));
    bootThread->setKernelStackTop(reinterpret_cast<void*>(stackPointer));

    CpuLocalData* cpu = cpu::getCurrent();
    cpu->setCurrentThread(bootThread);
    cpu->setCurrentTask(nullptr);
    serial::serialWrite("[THREAD] bootstrap thread initialized\n");
}

Thread* Thread::create(Task* task, ThreadEntry entryPoint, void* argument,
                       ThreadType type, bool makeRunnable) {
    if (entryPoint == nullptr) {
        return nullptr;
    }
    Thread* thread = static_cast<Thread*>(heap::kernelAllocate(sizeof(Thread)));
    const uint64_t stackSize = type == ThreadType::SystemService || (task && task->serviceRole() != os::ServiceRole::None) ? 256 * 1024 : kernelStackSize;
    uint8_t* stack = static_cast<uint8_t*>(heap::kernelAllocate(stackSize));
    if (thread == nullptr || stack == nullptr) {
        if (thread) heap::kernelFree(thread);
        if (stack) heap::kernelFree(stack);
        return nullptr;
    }
    Thread::clearThread(thread);
    thread->setId(nextThreadId.fetchAdd(1, MemoryOrder::Relaxed));
    thread->setState(ThreadState::Created);
    thread->setType(type);
    thread->setKernelStack(stack);
    thread->setKernelStackTop(stack + stackSize);
    thread->getContext().setStackPointer(reinterpret_cast<uint64_t>(
        Thread::buildInitialStack(thread->getKernelStackTop())));
    thread->getContext().setRip(reinterpret_cast<uint64_t>(Thread::bootstrap));
    thread->getContext().setRflags(0x202);
    thread->setTask(task);
    thread->setCpuId(cpu::getCurrentId());
    thread->setCpuAffinity(~0ULL);
    thread->setWeight(schedule::getSchedulerWeight(0));
    thread->setTimeSlice(time::milliseconds2Nanoseconds(4));
    thread->setEntryPoint(entryPoint);
    thread->setArgument(argument);
    thread->setState(ThreadState::Ready);
    if (task != nullptr && !task->attachThread(thread)) {
        heap::kernelFree(stack);
        heap::kernelFree(thread);
        return nullptr;
    }
    if (makeRunnable && schedule::isSchedulerInitialized()) {
        schedule::enqueueThread(thread);
    }
    return thread;
}

Thread* Thread::createUser(Task* task, uint64_t entryPoint,
                           uint64_t userStackPointer, uint64_t argument,
                           uint64_t initialStackSize, uint64_t initialAffinity, bool makeRunnable) {
    if (task == nullptr || entryPoint == 0 || userStackPointer == 0 ||
        entryPoint >= 0x0000800000000000ULL ||
        userStackPointer >= 0x0000800000000000ULL) {
        return nullptr;
    }
    if (task->isSystemService() && initialStackSize > 16 * 1024) return nullptr;
    Thread* thread = Thread::create(
        task, reinterpret_cast<ThreadEntry>(entryPoint),
        reinterpret_cast<void*>(userStackPointer),
        task->isSystemService() ? ThreadType::SystemService : ThreadType::User, false);
    if (thread != nullptr) {
        if (task->isSystemService()) {
            // Keep bootstrap's saved CPU context at the top untouched. Service
            // execution uses a supervisor stack shared by every address space.
            uint64_t stack = (reinterpret_cast<uint64_t>(thread->getKernelStackTop()) - 32 * 1024 - initialStackSize) & ~15ULL;
            if (initialStackSize && !memory::copyFromAddressSpace(
                    reinterpret_cast<void*>(stack), task->addressSpace().rootPhysicalAddress(),
                    userStackPointer, initialStackSize)) return nullptr;
            userStackPointer = stack;
        }
        thread->setUserStack(reinterpret_cast<void*>(userStackPointer));
        thread->setUserArgument(reinterpret_cast<void*>(argument));
        thread->setCpuAffinity(initialAffinity);
        if (makeRunnable && schedule::isSchedulerInitialized()) {
            schedule::enqueueThread(thread);
        }
    }
    return thread;
}

Thread* Thread::createForkedUser(Task* task, const SyscallContext& context,
                                 uint64_t signalMask) {
    if (task == nullptr || context.userRip >= 0x0000800000000000ULL ||
        context.userRsp >= 0x0000800000000000ULL) {
        return nullptr;
    }
    auto* childContext = static_cast<SyscallContext*>(
        heap::kernelAllocate(sizeof(SyscallContext)));
    if (childContext == nullptr) return nullptr;
    *childContext     = context;
    childContext->rax = 0;
    Thread* thread = Thread::create(task, Thread::forkedUserEntry, childContext,
                                    ThreadType::Kernel, false);
    if (thread != nullptr) {
        thread->setUserStack(reinterpret_cast<void*>(context.userRsp));
        thread->setSignalMask(signalMask);
        asm volatile("fxsave64 %0" : "=m"(thread->floatingPointState) : : "memory");
        if (schedule::isSchedulerInitialized())
            schedule::enqueueThread(thread);
    }
    return thread;
}

void Thread::bootstrap() {
    schedule::finishContextSwitch();
    interrupt::enable();
    Thread* thread = Thread::getCurrent();
    if (thread->getType() == ThreadType::User) {
        enterUserMode(reinterpret_cast<uint64_t>(thread->getEntryPoint()),
                      reinterpret_cast<uint64_t>(thread->getUserStack()),
                      reinterpret_cast<uint64_t>(thread->getUserArgument()));
    }
    if (thread->getType() == ThreadType::SystemService) {
        enterSystemService(reinterpret_cast<uint64_t>(thread->getEntryPoint()),
                           reinterpret_cast<uint64_t>(thread->getUserStack()),
                           reinterpret_cast<uint64_t>(thread->getUserArgument()));
    }
    thread->getEntryPoint()(thread->getArgument());
    Thread::exitCurrent(0);
}

[[noreturn]] void Thread::exitCurrent(int exitCode) {
    const uint64_t flags = interrupt::disable();
    Thread* thread       = Thread::getCurrent();
    thread->setExitCode(exitCode);
    thread->setState(ThreadState::Terminated);
    Endpoint::cancelThread(thread);
    ThreadCleanup::releaseAll(*thread);
    if (thread->getTask() != nullptr) {
        thread->getTask()->notifyThreadExit(thread);
    }
    schedule::requestReschedule();
    schedule::schedule();
    interrupt::restore(flags);
    cpu::halt();
}

void Thread::blockCurrent() {
    if (!Thread::getCurrent()->waitError()) schedule::blockCurrentThread();
    if (Thread::getCurrent()->waitError()) Thread::getCurrent()->cancelWait();
}

void Thread::prepareWait() { schedule::prepareCurrentWait(); }


void Thread::wake(Thread* thread) {
    schedule::wakeThread(thread);
}

void Thread::sleepCurrent(uint64_t milliseconds) {
    schedule::sleepCurrentThread(milliseconds);
}

void Thread::switchTo(Thread* next) {
    Thread* previous = this;
    // Kernel C++ uses general registers only, so the live FP state belongs
    // to the user thread even when it is blocked inside a system call.
    asm volatile("fxsave64 %0" : "=m"(floatingPointState) : : "memory");
    next->restoreFloatingPointState();
    switchThreadContext(&previous->getContext().accessStackPointer(),
                        next->getContext().getStackPointer());
}

Thread* Thread::getCurrent() { return cpu::getCurrent()->getCurrentThread(); }

void Thread::terminateCurrentFromException(int exitCode) {
    Thread* thread = Thread::getCurrent();
    if (thread == nullptr || thread->getType() != ThreadType::User) {
        cpu::halt();
    }
    Thread::exitCurrent(exitCode);
}

}  // namespace kernel

void kernel::Thread::clearThread(kernel::Thread* thread) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(thread);
    for (uint64_t index = 0; index < sizeof(kernel::Thread); ++index) {
        bytes[index] = 0;
    }
    thread->resetFloatingPointState();
}
void kernel::Thread::resetFloatingPointState() {
    for (auto& byte : floatingPointState) byte = 0;
    // FXSAVE layout: x87 control word and MXCSR; empty x87 tag word.
    floatingPointState[0] = 0x7f;
    floatingPointState[1] = 0x03;
    floatingPointState[24] = 0x80;
    floatingPointState[25] = 0x1f;
}
void kernel::Thread::restoreFloatingPointState() {
    asm volatile("fxrstor64 %0" : : "m"(floatingPointState) : "memory");
}
uint64_t* kernel::Thread::buildInitialStack(void* stackTop) {
    uint64_t* stack = static_cast<uint64_t*>(stackTop);
    *--stack        = 0;
    *--stack        = reinterpret_cast<uint64_t>(kernel::Thread::bootstrap);
    *--stack        = 0x202;
    for (uint32_t registerIndex = 0; registerIndex < 15; ++registerIndex) {
        *--stack = 0;
    }
    return stack;
}
void kernel::Thread::forkedUserEntry(void* argument) {
    auto* context          = static_cast<kernel::SyscallContext*>(argument);
    kernel::Thread* thread = kernel::Thread::getCurrent();
    if (context == nullptr || thread == nullptr) kernel::cpu::halt();
    kernel::SyscallContext saved = *context;
    kernel::heap::kernelFree(context);
    thread->setType(kernel::ThreadType::User);
    kernel::resumeUserContext(&saved);
}
