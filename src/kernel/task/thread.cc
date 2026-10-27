/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/thread.cc
 * Thread creation, bootstrap stacks, and execution-context switching.
 */

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"

namespace kernel::task {

static_assert(sizeof(CpuContext) == 21 * sizeof(uint64_t));

namespace {

// Ordinary kernel stack bytes; fixed-role services use a larger stack to
// accommodate deeper native service/loader paths without changing user stacks.
constexpr uint64_t kernelStackSize = 64 * 1024;

}  // namespace

// Wrap the already executing bootstrap context rather than allocating a
// replacement stack. Each CPU records its own current thread before local
// scheduler initialization; a Task can be attached later.
void initThreads() {
    Thread* bootThread = static_cast<Thread*>(mm::allocateHeap(sizeof(Thread)));
    if (bootThread == nullptr) {
        device::serialWrite("[THREAD] unable to allocate bootstrap thread\n");
        cpu::halt();
    }
    bootThread->clearStorage();
    bootThread->setId(task::allocateId());
    bootThread->setState(ThreadState::Running);
    bootThread->setType(ThreadType::Kernel);
    bootThread->setCpuAffinity(~0ULL);
    bootThread->setWeight(1024);
    bootThread->setTimeSlice(time::milliseconds2Nanoseconds(4));
    bootThread->setCpuId(cpu::getCurrentId());
    uint64_t stackPointer;
    asm volatile("movq %%rsp, %0" : "=r"(stackPointer));
    bootThread->setKernelStackTop(reinterpret_cast<void*>(stackPointer));

    ::kernel::cpu::CpuLocalData* cpu = cpu::getCurrent();
    cpu->setCurrentThread(bootThread);
    cpu->setCurrentTask(nullptr);
    device::serialWrite("[THREAD] bootstrap thread initialized\n");
}

// Allocate a detached thread and kernel stack, build a frame matching
// switchThreadContext, then attach to the owner. makeRunnable lets callers
// finish user state before enqueueing. Failure before attachment releases both
// allocations; a successful attachment transfers lifetime to the Task.
Thread* Thread::create(Task* task, ThreadEntry entryPoint, void* argument,
                       ThreadType type, bool makeRunnable) {
    if (entryPoint == nullptr) {
        return nullptr;
    }
    Thread* thread = static_cast<Thread*>(mm::allocateHeap(sizeof(Thread)));
    const uint64_t stackSize =
        type == ThreadType::Sys ||
                (task && task->serviceRole() != os::ServiceRole::None)
            ? 256 * 1024
            : kernelStackSize;
    uint8_t* stack = static_cast<uint8_t*>(mm::allocateHeap(stackSize));
    if (thread == nullptr || stack == nullptr) {
        if (thread) mm::freeHeap(thread);
        if (stack) mm::freeHeap(stack);
        return nullptr;
    }
    thread->clearStorage();
    thread->setId(task::allocateId());
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
    const auto* creator = currentThread();
    // New threads inherit the creating thread's nice within the same process;
    // a new process uses the initial nice recorded at task creation.
    thread->setNice(!task ? 0
                    : creator && creator->getTask() == task
                        ? creator->getNice()
                        : task->initialNice());
    thread->setWeight(task::getSchedulerWeight(thread->getNice()));
    thread->setTimeSlice(time::milliseconds2Nanoseconds(4));
    thread->setEntryPoint(entryPoint);
    thread->setArgument(argument);
    thread->setState(ThreadState::Ready);
    if (task != nullptr && !task->attachThread(thread)) {
        mm::freeHeap(stack);
        mm::freeHeap(thread);
        return nullptr;
    }
    if (makeRunnable && task::isSchedulerInitialized()) {
        task::enqueueThread(thread);
    }
    return thread;
}

// Validate lower-half entry and stack addresses, then reuse the generic
// creator without enqueueing. Configure user state, inherited signal/FP state
// and initial affinity before allowing any CPU to select the thread. The
// legacy Sys path copies an initial stack onto supervisor-owned storage.
Thread* Thread::createUser(Task* task, uint64_t entryPoint,
                           uint64_t userStackPointer, uint64_t argument,
                           uint64_t initialStackSize, uint64_t initialAffinity,
                           bool makeRunnable) {
    if (task == nullptr || entryPoint == 0 || userStackPointer == 0 ||
        entryPoint >= 0x0000800000000000ULL ||
        userStackPointer >= 0x0000800000000000ULL) {
        return nullptr;
    }
    if (task->isSystemService() && initialStackSize > 16 * 1024) return nullptr;
    Thread* thread = Thread::create(
        task, reinterpret_cast<ThreadEntry>(entryPoint),
        reinterpret_cast<void*>(userStackPointer),
        task->isSystemService() ? ThreadType::Sys : ThreadType::User, false);
    if (thread != nullptr) {
        if (task->isSystemService()) {
            // Keep bootstrap's saved CPU context at the top untouched. Service
            // execution uses a supervisor stack shared by every address space.
            uint64_t stack =
                (reinterpret_cast<uint64_t>(thread->getKernelStackTop()) -
                 32 * 1024 - initialStackSize) &
                ~15ULL;
            if (initialStackSize &&
                !mm::copyFromAddressSpace(
                    reinterpret_cast<void*>(stack),
                    task->addressSpace().rootPhysicalAddress(),
                    userStackPointer, initialStackSize))
                return nullptr;
            userStackPointer = stack;
        }
        thread->setUserStack(reinterpret_cast<void*>(userStackPointer));
        thread->setUserArgument(reinterpret_cast<void*>(argument));
        thread->lastUserContext            = {};
        thread->lastUserContext.userRip    = entryPoint;
        thread->lastUserContext.userRsp    = userStackPointer;
        thread->lastUserContext.userRflags = 0x202;
        thread->hasUserContext             = true;
        Thread* creator                    = currentThread();
        if (creator && creator->getTask() == task) {
            thread->setSignalMask(creator->getSignalMask());
            asm volatile("fxsave64 %0"
                         : "=m"(thread->floatingPointState)
                         :
                         : "memory");
        }
        thread->setCpuAffinity(initialAffinity);
        if (makeRunnable && task::isSchedulerInitialized()) {
            task::enqueueThread(thread);
        }
    }
    return thread;
}

// Keep the copied syscall frame on the heap until the child first runs. The
// child returns zero from the same user continuation and inherits the caller's
// signal-frame chain/FP state. Its temporary Kernel entry changes to User
// before resuming that saved context.
Thread* Thread::createForkedUser(Task* task, const SyscallContext& context,
                                 uint64_t signalMask) {
    if (task == nullptr || context.userRip >= 0x0000800000000000ULL ||
        context.userRsp >= 0x0000800000000000ULL) {
        return nullptr;
    }
    auto* childContext =
        static_cast<SyscallContext*>(mm::allocateHeap(sizeof(SyscallContext)));
    if (childContext == nullptr) return nullptr;
    *childContext     = context;
    childContext->rax = 0;
    Thread* thread = Thread::create(task, Thread::forkedUserEntry, childContext,
                                    ThreadType::Kernel, false);
    if (thread != nullptr) {
        thread->setUserStack(reinterpret_cast<void*>(context.userRsp));
        thread->setSignalMask(signalMask);
        thread->lastUserContext = *childContext;
        thread->hasUserContext  = true;
        if (currentThread()) {
            thread->signalStack       = currentThread()->signalStack;
            thread->activeSignalFrame = currentThread()->activeSignalFrame;
            thread->signalDepth       = currentThread()->signalDepth;
        }
        asm volatile("fxsave64 %0"
                     : "=m"(thread->floatingPointState)
                     :
                     : "memory");
        if (task::isSchedulerInitialized()) task::enqueueThread(thread);
    }
    return thread;
}

// A newly selected thread has no earlier schedule call to return through, so
// it must complete switch bookkeeping itself before enabling interrupts.
// User/Sys entries do not return; a returning kernel entry exits its thread.
void Thread::bootstrap() {
    task::finishContextSwitch();
    cpu::enableInterrupts();
    Thread* thread = currentThread();
    if (thread->getType() == ThreadType::User) {
        enterUserMode(reinterpret_cast<uint64_t>(thread->getEntryPoint()),
                      reinterpret_cast<uint64_t>(thread->getUserStack()),
                      reinterpret_cast<uint64_t>(thread->getUserArgument()));
    }
    if (thread->getType() == ThreadType::Sys) {
        enterSystemService(
            reinterpret_cast<uint64_t>(thread->getEntryPoint()),
            reinterpret_cast<uint64_t>(thread->getUserStack()),
            reinterpret_cast<uint64_t>(thread->getUserArgument()));
    }
    thread->getEntryPoint()(thread->getArgument());
    exitCurrentThread(0);
}

// Save this thread's live FP state, restore next's, then exchange kernel RSP
// values through assembly. Execution after switchThreadContext resumes only
// when this saved continuation is later scheduled again.
void Thread::switchTo(Thread* next) {
    Thread* previous = this;
    // Kernel C++ uses general registers only, so the live FP state belongs to
    // the user thread even when it is blocked inside a system call.
    asm volatile("fxsave64 %0" : "=m"(floatingPointState) : : "memory");
    next->restoreFloatingPointState();
    switchThreadContext(&previous->getContext().accessStackPointer(),
                        next->getContext().getStackPointer());
}

// Current CPU thread, not a process-wide main-thread lookup.
Thread* currentThread() { return cpu::getCurrent()->getCurrentThread(); }

// Initialize newly allocated raw storage before publication. Resetting a live
// thread this way would destroy queue links and cleanup ownership.
void Thread::clearStorage() {
    Thread* thread = this;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(thread);
    for (uint64_t index = 0; index < sizeof(Thread); ++index) {
        bytes[index] = 0;
    }
    thread->resetFloatingPointState();
}
// Create an empty FXSAVE image with the default masked x87 control word and
// MXCSR. Thread switches and signal handlers restore this hardware layout.
void Thread::resetFloatingPointState() {
    for (auto& byte : floatingPointState) byte = 0;
    // FXSAVE layout: x87 control word and MXCSR; empty x87 tag word.
    floatingPointState[0]  = 0x7f;
    floatingPointState[1]  = 0x03;
    floatingPointState[24] = 0x80;
    floatingPointState[25] = 0x1f;
}
// Restore the aligned 512-byte native floating-point save area.
void Thread::restoreFloatingPointState() {
    asm volatile("fxrstor64 %0" : : "m"(floatingPointState) : "memory");
}
// Match the fifteen GPR pops, popfq, and ret in switchThreadContext. The
// synthetic return address enters bootstrap with ABI-compatible stack
// alignment; changing this layout requires changing the assembly together.
uint64_t* Thread::buildInitialStack(void* stackTop) {
    uint64_t* stack = static_cast<uint64_t*>(stackTop);
    *--stack        = 0;
    *--stack        = reinterpret_cast<uint64_t>(Thread::bootstrap);
    *--stack        = 0x202;
    for (uint32_t registerIndex = 0; registerIndex < 15; ++registerIndex) {
        *--stack = 0;
    }
    return stack;
}
// Move the temporary heap frame into this stack before releasing it.
// resumeUserContext copies that frame again into the unified IRET layout, so
// the temporary allocation is not needed once user execution resumes.
void Thread::forkedUserEntry(void* argument) {
    auto* context  = static_cast<SyscallContext*>(argument);
    Thread* thread = currentThread();
    if (context == nullptr || thread == nullptr) kernel::cpu::halt();
    SyscallContext saved = *context;
    kernel::mm::freeHeap(context);
    thread->setType(ThreadType::User);
    resumeUserContext(&saved);
}

// Publish the thread under the task lock only while new threads are accepted.
// The first thread becomes main and takes the Task ID; subsequent threads
// retain their separately allocated IDs.
bool Task::attachThread(Thread* thread) {
    if (thread == nullptr) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    if (!acceptsThreads_) {
        return false;
    }
    thread->setTask(this);
    thread->setTaskNext(threads_);
    threads_ = thread;
    ++threadCount_;
    if (mainThread_ == nullptr) {
        thread->setId(id_);
        mainThread_ = thread;
    }
    state_ = TaskState::Running;
    return true;
}

}  // namespace kernel::task
