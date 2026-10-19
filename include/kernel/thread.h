#pragma once

#include <stdint.h>

namespace kernel {

class Task;
class SpinLock;
class Endpoint;
class Thread;
class ThreadCleanup {
   public:
    ~ThreadCleanup();
    void attach(void* object, void (*release)(void*));
    void detach();
    void reset();
    static void releaseAll(Thread& thread);
   private:
    Thread* owner_ = nullptr;
    ThreadCleanup* previous_ = nullptr;
    ThreadCleanup* next_ = nullptr;
    void* object_ = nullptr;
    void (*release_)(void*) = nullptr;
};
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

enum class ThreadType : uint8_t { Kernel, User, SystemService };

using ThreadEntry = void (*)(void* argument);

class CpuContext {
   public:
    uint64_t getStackPointer() const { return stackPointer; }
    uint64_t setStackPointer(uint64_t value) {
        stackPointer = value;
        return stackPointer;
    }
    uint64_t& accessStackPointer() { return stackPointer; }
    uint64_t setRip(uint64_t value) {
        rip = value;
        return rip;
    }
    uint64_t setRflags(uint64_t value) {
        rflags = value;
        return rflags;
    }

   private:
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

class Thread {
   public:
    bool migrate(uint32_t cpuId);
    bool setAffinity(uint64_t cpuMask);
    static void init();
    static Thread* create(Task* task, ThreadEntry entryPoint, void* argument,
                          ThreadType type   = ThreadType::Kernel,
                          bool makeRunnable = true);
    static Thread* createUser(Task* task, uint64_t entryPoint,
                              uint64_t userStackPointer, uint64_t argument = 0,
                              uint64_t initialStackSize = 0);
    static Thread* createForkedUser(Task* task, const SyscallContext& context,
                                    uint64_t signalMask);
    [[noreturn]] static void exitCurrent(int exitCode);
    static void blockCurrent();
    static void prepareWait();
    void registerWait(SpinLock& lock, Thread*& head, Thread** tail = nullptr);
    void clearWaitRegistration();
    void cancelWait();
    Endpoint* getActiveEndpoint() const { return activeEndpoint; }
    void setActiveEndpoint(Endpoint* endpoint) { activeEndpoint = endpoint; }
    bool isExitAccounted() const { return exitAccounted; }
    void setExitAccounted(bool value) { exitAccounted = value; }
    bool hasCleanup() const { return cleanupHead != nullptr; }
    ThreadCleanup*& cleanupList() { return cleanupHead; }
    void setIpcWaitResult(int* result, bool immediate = false) { ipcWaitResult = result; ipcImmediate = immediate; }
    int waitError() const {
        const int result = ipcWaitResult ? __atomic_load_n(ipcWaitResult, __ATOMIC_ACQUIRE) : 0;
        return result < 0 ? result : (ipcImmediate ? -6 : 0);
    }
    bool hasActiveIpcCall() const { return __atomic_load_n(&activeIpcCall, __ATOMIC_ACQUIRE); }
    void setActiveIpcCall(bool active) { __atomic_store_n(&activeIpcCall, active, __ATOMIC_RELEASE); }
    static void wake(Thread* thread);
    static void sleepCurrent(uint64_t milliseconds);
    void switchTo(Thread* next);
    void resetFloatingPointState();
    void restoreFloatingPointState();
    static Thread* getCurrent();
    void bindSyscallContext(SyscallContext* context) { syscallContext_ = context; contextRestored_ = false; }
    SyscallContext* syscallContext() const { return syscallContext_; }
    void markContextRestored() { contextRestored_ = true; }
    bool contextRestored() const { return contextRestored_; }
    void clearSyscallContext() { syscallContext_ = nullptr; contextRestored_ = false; }
    static void terminateCurrentFromException(int exitCode);

    static void bootstrap();

    uint64_t getId() const { return id; }
    uint64_t setId(uint64_t value) {
        id = value;
        return id;
    }
    ThreadState getState() const { return __atomic_load_n(&state, __ATOMIC_ACQUIRE); }
    ThreadState setState(ThreadState value) {
        __atomic_store_n(&state, value, __ATOMIC_RELEASE);
        return value;
    }
    ThreadType getType() const { return type; }
    ThreadType setType(ThreadType value) {
        type = value;
        return type;
    }
    void* getKernelStack() const { return kernelStack; }
    void* setKernelStack(void* value) {
        kernelStack = value;
        return kernelStack;
    }
    void* getKernelStackTop() const { return kernelStackTop; }
    void* setKernelStackTop(void* value) {
        kernelStackTop = value;
        return kernelStackTop;
    }
    void* getUserStack() const { return userStack; }
    void* setUserStack(void* value) {
        userStack = value;
        return userStack;
    }
    CpuContext& getContext() { return context; }
    const CpuContext& getContext() const { return context; }
    Task* getTask() const { return task; }
    Task* setTask(Task* value) {
        task = value;
        return task;
    }
    uint64_t getCpuId() const { return cpuId; }
    uint64_t setCpuId(uint64_t value) {
        cpuId = value;
        return cpuId;
    }
    uint64_t getCpuAffinity() const { return cpuAffinity; }
    uint64_t setCpuAffinity(uint64_t value) {
        cpuAffinity = value;
        return cpuAffinity;
    }
    uint64_t getVruntime() const { return vruntime; }
    uint64_t setVruntime(uint64_t value) {
        vruntime = value;
        return vruntime;
    }
    uint64_t getRuntime() const { return runtime; }
    uint64_t setRuntime(uint64_t value) {
        runtime = value;
        return runtime;
    }
    uint64_t getTimeSlice() const { return timeSlice; }
    uint64_t setTimeSlice(uint64_t value) {
        timeSlice = value;
        return timeSlice;
    }
    uint64_t getSliceRuntime() const { return sliceRuntime; }
    uint64_t setSliceRuntime(uint64_t value) {
        sliceRuntime = value;
        return sliceRuntime;
    }
    uint32_t getWeight() const { return weight; }
    uint32_t setWeight(uint32_t value) {
        weight = value;
        return weight;
    }
    ThreadEntry getEntryPoint() const { return entryPoint; }
    ThreadEntry setEntryPoint(ThreadEntry value) {
        entryPoint = value;
        return entryPoint;
    }
    void* getArgument() const { return argument; }
    void* setArgument(void* value) {
        argument = value;
        return argument;
    }
    void* getUserArgument() const { return userArgument; }
    void* setUserArgument(void* value) {
        userArgument = value;
        return userArgument;
    }
    int getExitCode() const { return exitCode; }
    int setExitCode(int value) {
        exitCode = value;
        return exitCode;
    }
    Thread* getRbParent() const { return rbParent; }
    Thread* setRbParent(Thread* value) {
        rbParent = value;
        return rbParent;
    }
    Thread* getRbLeft() const { return rbLeft; }
    Thread* setRbLeft(Thread* value) {
        rbLeft = value;
        return rbLeft;
    }
    Thread*& accessRbLeft() { return rbLeft; }
    Thread* getRbRight() const { return rbRight; }
    Thread* setRbRight(Thread* value) {
        rbRight = value;
        return rbRight;
    }
    Thread*& accessRbRight() { return rbRight; }
    bool getRbRed() const { return rbRed; }
    bool setRbRed(bool value) {
        rbRed = value;
        return rbRed;
    }
    bool getInRunQueue() const { return inRunQueue; }
    bool setInRunQueue(bool value) {
        inRunQueue = value;
        return inRunQueue;
    }
    bool& accessWakePending() { return wakePending; }
    ThreadState getStateBeforeStop() const { return stateBeforeStop; }
    ThreadState setStateBeforeStop(ThreadState value) {
        stateBeforeStop = value;
        return stateBeforeStop;
    }
    Thread* getWaitNext() const { return waitNext; }
    Thread* setWaitNext(Thread* value) {
        waitNext = value;
        return waitNext;
    }
    Thread*& accessWaitNext() { return waitNext; }
    Thread* getJoinWaiters() const { return joinWaiters; }
    Thread*& accessJoinWaiters() { return joinWaiters; }
    Thread*& accessJoinWaitersTail() { return joinWaitersTail; }
    Thread* setJoinWaiters(Thread* value) {
        joinWaiters = value;
        return joinWaiters;
    }
    Thread* getJoinWaitersTail() const { return joinWaitersTail; }
    Thread* setJoinWaitersTail(Thread* value) {
        joinWaitersTail = value;
        return joinWaitersTail;
    }
    volatile uint32_t* getWaitAddress() const { return waitAddress; }
    volatile uint32_t* setWaitAddress(volatile uint32_t* value) {
        waitAddress = value;
        return waitAddress;
    }
    uint64_t getWaitKey() const { return waitKey; }
    uint64_t setWaitKey(uint64_t value) {
        waitKey = value;
        return waitKey;
    }
    uint64_t getSignalMask() const { return signalMask; }
    uint64_t setSignalMask(uint64_t value) {
        signalMask = value;
        return signalMask;
    }
    uint64_t getPendingSignals() const { return pendingSignals; }
    uint64_t setPendingSignals(uint64_t value) {
        pendingSignals = value;
        return pendingSignals;
    }
    Thread* getTaskNext() const { return taskNext; }
    Thread* setTaskNext(Thread* value) {
        taskNext = value;
        return taskNext;
    }
    uint64_t getWakeDeadlineNanoseconds() const {
        return wakeDeadlineNanoseconds;
    }
    uint64_t setWakeDeadlineNanoseconds(uint64_t value) {
        wakeDeadlineNanoseconds = value;
        return wakeDeadlineNanoseconds;
    }

   private:
    static void clearThread(Thread* thread);
    static uint64_t* buildInitialStack(void* stackTop);
    static void forkedUserEntry(void* argument);

    uint64_t id;
    ThreadState state;
    ThreadType type;
    void* kernelStack;
    void* kernelStackTop;
    void* userStack;
    CpuContext context;
    Task* task;
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
    bool activeIpcCall;
    bool exitAccounted;
    ThreadCleanup* cleanupHead;
    SyscallContext* syscallContext_;
    bool contextRestored_;
    int* ipcWaitResult;
    bool ipcImmediate;
    Endpoint* activeEndpoint;
    SpinLock* waitOwnerLock;
    Thread** waitOwnerHead;
    Thread** waitOwnerTail;
    ThreadState stateBeforeStop;
    Thread* waitNext;
    Thread* joinWaiters;
    Thread* joinWaitersTail;
    volatile uint32_t* waitAddress;
    uint64_t waitKey;
    uint64_t signalMask;
    uint64_t pendingSignals;
    Thread* taskNext;
    uint64_t wakeDeadlineNanoseconds;
    alignas(16) uint8_t floatingPointState[512];
};

extern "C" [[noreturn]] void enterUserMode(uint64_t entryPoint,
                                           uint64_t stackPointer,
                                           uint64_t argument);
extern "C" [[noreturn]] void enterSystemService(uint64_t entryPoint,
                                                uint64_t stackPointer,
                                                uint64_t argument);
extern "C" [[noreturn]] void resumeUserContext(SyscallContext* context);
extern "C" void switchThreadContext(uint64_t* previousStackPointer,
                                    uint64_t nextStackPointer);

}  // namespace kernel
