#include "service/common/records.h"
#pragma once
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"

#include <cstdint>
#include <os/syscall.h>

namespace kernel {

struct SyscallContext;
class Task;
namespace interrupt { struct Context; }

class TaskMechanisms {
   public:
    explicit TaskMechanisms(Task& caller);
    uint64_t createThread(uint64_t entry, uint64_t stack, uint64_t argument);
    uint64_t fork();
    bool replace(uint64_t image, uint64_t& failure);
   private:
    Task& caller_;
};

enum class FaultAccess : uint8_t { Unknown, Read, Write, Execute };
enum class FaultOrigin : uint8_t { Asynchronous, UserException, ServiceException };
struct FaultRecord {
    uint64_t pid = 0, tid = 0, started = 0, timestamp = 0;
    uint64_t vector = 0, error = 0, rip = 0, rsp = 0, address = 0, debugStatus = 0;
    uint32_t signal = 0;
    int32_t code = 0;
    FaultAccess access = FaultAccess::Unknown;
    bool addressValid = false;
    FaultOrigin origin = FaultOrigin::Asynchronous;
    uint8_t reserved[5]{};
};

enum class Signal : uint32_t {
    Hangup                = 1,
    Interrupt             = 2,
    Quit                  = 3,
    IllegalInstruction    = 4,
    Trap                  = 5,
    Abort                 = 6,
    Bus                   = 7,
    FloatingPoint         = 8,
    Kill                  = 9,
    User1                 = 10,
    SegmentationViolation = 11,
    User2                 = 12,
    BrokenPipe            = 13,
    Alarm                 = 14,
    Terminate             = 15,
    StackFault            = 16,
    Child                 = 17,
    Continue              = 18,
    Stop                  = 19,
    TerminalStop          = 20,
    TerminalInput         = 21,
    TerminalOutput        = 22,
    Urgent                = 23,
    CpuLimit              = 24,
    FileSizeLimit         = 25,
    VirtualAlarm          = 26,
    Profiling             = 27,
    WindowChanged         = 28
};

constexpr uint32_t maximumSignal       = 64;
constexpr uint64_t signalDefault       = 0;
constexpr uint64_t signalIgnore        = 1;
constexpr uint64_t signalFlagNoDefer   = 0x40000000ULL;
constexpr uint64_t signalFlagResetHand = 0x80000000ULL;
constexpr uint64_t signalFlagOnStack = 0x08000000ULL;

class SignalAction {
   public:
    SignalAction() = default;
    SignalAction(uint64_t handlerValue, uint64_t maskValue, uint64_t flagsValue,
                 uint64_t restorerValue)
        : handler(handlerValue),
          mask(maskValue),
          flags(flagsValue),
          restorer(restorerValue) {}
    uint64_t getHandler() const { return handler; }
    uint64_t getMask() const { return mask; }
    uint64_t getFlags() const { return flags; }
    uint64_t setFlags(uint64_t value) {
        flags = value;
        return flags;
    }
    uint64_t getRestorer() const { return restorer; }
    uint64_t& accessRestorer() { return restorer; }

   private:
    uint64_t handler;
    uint64_t mask;
    uint64_t flags;
    uint64_t restorer;
};

namespace signal {
bool isValidSignal(uint32_t signal);
bool sendSignalToTask(uint64_t taskId, uint32_t signal, Task* caller = nullptr);
bool sendSignalToTaskGroup(uint64_t taskGroupId, uint32_t signal, Task* caller = nullptr);
bool deliverPendingSignal(SyscallContext* context);
bool restoreSignalFrame(SyscallContext* context, uint64_t userFrame);
void returnToUser(interrupt::Context* context);
void captureUserContext(const interrupt::Context* context);
void raiseException(interrupt::Context* context, uint32_t signal, int code,
                    uint64_t address = 0, bool addressValid = false,
                    FaultAccess access = FaultAccess::Unknown, uint64_t debugStatus = 0);
[[noreturn]] void fatal(uint32_t signal, const FaultRecord* fault = nullptr);
bool hasCoreDefault(uint32_t signal);
void toMachineContext(const SyscallContext& source, OsMachineContext& target);
void fromMachineContext(const OsMachineContext& source, SyscallContext& target);

}  // namespace signal

}  // namespace kernel


#include "kernel/syscall.h"

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

enum class ThreadType : uint8_t { Kernel, User, Sys };

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
                              uint64_t initialStackSize = 0,
                              uint64_t initialAffinity = ~0ULL,
                              bool makeRunnable = true);
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
    bool isStackRetired() const { return stackRetired; }
    void setStackRetired(bool value) { stackRetired = value; }
    bool isJoinConsumed() const { return joinConsumed; }
    void setJoinConsumed(bool value) { joinConsumed = value; }
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
    void saveFloatingPointState() { asm volatile("fxsave64 %0" : "=m"(floatingPointState) : : "memory"); }
    uint8_t* fpState() { return floatingPointState; }
    OsSignalStack signalStack{};
    uint64_t activeSignalFrame = 0;
    uint32_t signalDepth = 0;
    uint64_t signalGeneration = 0;
    SyscallContext lastUserContext{};
    bool hasUserContext = false;
    FaultRecord lastFault{};
    static Thread* getCurrent();
    void bindSyscallContext(SyscallContext* context) { syscallContext_ = context; contextRestored_ = false; }
    SyscallContext* syscallContext() const { return syscallContext_; }
    void markContextRestored() { contextRestored_ = true; }
    bool contextRestored() const { return contextRestored_; }
    void clearSyscallContext() { syscallContext_ = nullptr; contextRestored_ = false; }

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
    int32_t getNice() const { return __atomic_load_n(&nice, __ATOMIC_RELAXED); }
    void setNice(int32_t value) { __atomic_store_n(&nice, value, __ATOMIC_RELAXED); }
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
    Thread*& accessTaskNext() { return taskNext; }
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
    bool stackRetired;
    bool joinConsumed;
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


#include "kernel/elf.h"

#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>
#include "kernel/spinlock.h"


namespace kernel {

class Thread;
class Task;
class TaskReference {
   public:
    TaskReference(Task* task = nullptr);
    TaskReference(TaskReference&& other);
    TaskReference& operator=(TaskReference&& other);
    ~TaskReference();
    TaskReference(const TaskReference&) = delete;
    TaskReference& operator=(const TaskReference&) = delete;
    Task* operator->() const { return task_; }
    operator Task*() const { return task_; }
   private:
    Task* task_;
    ThreadCleanup cleanup_;
};
namespace task {
uint64_t allocateId();
void init();
TaskReference find(uint64_t taskId);
TaskReference at(uint32_t index);
TaskReference next(uint64_t minimumId);
uint64_t terminalForeground(uint32_t kind, uint32_t index);
bool setTerminalForeground(uint32_t kind, uint32_t index, uint64_t group);
}  // namespace task

enum class TaskState : uint8_t { Created, Running, Terminated, Stopped };

class AddressSpace {
   public:
    void init(uint64_t rootPhysicalAddress);
    uint64_t replaceRoot(uint64_t rootPhysicalAddress);
    uint64_t rootPhysicalAddress() const;
    uint64_t generation() const { return generation_; }
    SpinLock& lock();

   private:
    uint64_t rootPhysicalAddress_;
    uint64_t generation_ = 1;
    SpinLock lock_;
};

enum class HandleType : uint16_t {
    None,
    Endpoint,
    Task,
    MmioRegion,
    DmaBuffer,
    InterruptBinding,
    SharedMemory,
    PageObject,
    DeviceDomain,
    FramebufferMapping
};

constexpr uint32_t handleRightRead   = 1U << 0;
constexpr uint32_t handleRightWrite  = 1U << 1;
constexpr uint32_t handleRightManage = 1U << 2;

class HandleTable {
   public:
    class Transfer;
    class Pin;
    uint32_t capacity();
    bool init(uint32_t capacity, Task* owner = nullptr, bool escrow = false);
    uint64_t add(void* object, HandleType type, uint32_t rights);
    bool addAt(uint64_t handle, void* object, HandleType type, uint32_t rights);
    void* get(uint64_t handle, HandleType type, uint32_t requiredRights);
    bool close(uint64_t handle);
    void closeAll();
    void dispose();
    uint64_t duplicate(uint64_t handle, uint32_t rights, uint32_t flags = 0);
    uint64_t copyTo(HandleTable& destination, uint64_t handle,
                    uint64_t destinationHandle = 0);
    bool restrictRights(uint64_t handle, uint32_t rights);
    bool setFlags(uint64_t handle, uint32_t flags);
    void closeOnExec();
    bool getFlags(uint64_t handle, uint32_t& flags);
    int64_t references(uint64_t handle);
    bool hasObjectRights(void* object, HandleType type, uint32_t rights);
    bool inspect(uint64_t handle, void** object, HandleType* type,
                 uint32_t* rights);
    bool inspectAt(uint32_t index, uint64_t* handle, void** object,
                   HandleType* type, uint32_t* rights);

   private:
    struct Reference {
        void* object;
        HandleType type;
        uint32_t count;
        uint32_t escrow = 0;
        static void retain(Reference* reference);
        static void release(Reference* reference);
    };
    uint64_t installLocked(Reference* reference, uint32_t rights, uint32_t flags);
    bool validLocked(uint64_t handle) const;
    bool growLocked(uint32_t required);
    class HandleEntry {
       public:
        Reference* reference;
        uint32_t flags;
        HandleEntry() = default;
        HandleEntry(void* objectValue, uint32_t rightsValue, uint16_t typeValue,
                    uint16_t generationValue)
            : object(objectValue),
              rights(rightsValue),
              type(typeValue),
              generation(generationValue) {}
        void* getObject() const { return object; }
        void* setObject(void* value) {
            object = value;
            return object;
        }
        uint32_t getRights() const { return rights; }
        uint32_t setRights(uint32_t value) {
            rights = value;
            return rights;
        }
        uint16_t getType() const { return type; }
        uint16_t setType(uint16_t value) {
            type = value;
            return type;
        }
        uint16_t getGeneration() const { return generation; }
        uint16_t setGeneration(uint16_t value) {
            generation = value;
            return generation;
        }

       private:
        void* object;
        uint32_t rights;
        uint16_t type;
        uint16_t generation;
    };

    HandleEntry* entries_;
    uint32_t capacity_;
    SpinLock lock_;
    Task* owner_ = nullptr;
    bool disposed_ = false;
    bool escrow_ = false;
};

class HandleTable::Pin {
   public:
    Pin(HandleTable& table, uint64_t handle, HandleType type, uint32_t rights);
    ~Pin();
    Pin(const Pin&) = delete;
    Pin& operator=(const Pin&) = delete;
    template <typename T> T* as() const {
        return reference_ ? static_cast<T*>(reference_->object) : nullptr;
    }
   private:
    Reference* reference_ = nullptr;
    ThreadCleanup cleanup_;
};

// A queued message owns references, not handles in an endpoint owner's table.
class HandleTable::Transfer {
   public:
    bool capture(HandleTable& source, const os::IpcMessage& message, bool commitMove = true);
    bool install(HandleTable& destination, os::IpcMessage& message);
    void clear();

   private:
    Reference* references_[4]{};
    uint32_t rights_[4]{};
    uint32_t count_ = 0;
    HandleTable* moveSource_ = nullptr;
    uint64_t moveHandles_[4]{};
    bool installLocked(HandleTable& destination, os::IpcMessage& message);
};

constexpr uint64_t taskCapabilityDevice  = 1ULL << 0;
constexpr uint64_t taskCapabilitySpawn   = 1ULL << 1;
constexpr uint64_t taskCapabilityDisplay = 1ULL << 2;
constexpr uint64_t taskCapabilityUsers = 1ULL << 3;
constexpr uint64_t taskCapabilityPci = 1ULL << 4;
constexpr uint64_t taskCapabilityMemory = 1ULL << 5;
constexpr uint64_t taskCapabilityReadIdentity = 1ULL << 6;

class Task {
   public:
    static Task* create(const char* name, Task* parent = nullptr);
    void retain();
    void release();
    static void setInitTask(Task* task);

    uint64_t id() const;
    const char* name() const;
    TaskState state();
    AddressSpace& addressSpace();
    HandleTable& handles();
    Task* parent();
    uint64_t parentId();
    TaskReference parentReference();
    bool isTerminated();
    bool resourcesReclaimed() const { return __atomic_load_n(&resourceState_, __ATOMIC_ACQUIRE) == 2U; }
    bool setMemoryFrozen(bool frozen);
    bool memoryFrozen() const { return __atomic_load_n(&memoryFrozen_, __ATOMIC_ACQUIRE); }
    bool claimMemoryFreezeExpiry(uint64_t now);
    void authentication(uint64_t& session, uint32_t& uid, uint32_t& gid);
    bool authenticate(uint32_t uid, uint32_t gid, bool preserveCredentials, uint32_t groupCount = 0, const uint32_t* groups = nullptr);
    int exitCode();
    void recordSignalExit(uint32_t signal, const FaultRecord* fault = nullptr);
    void getExitInfo(os::wire::ExitInfo& info);
    OsSignalInfo childSignalInfo();
    void notifyChildExit(Task& child);
    bool dumpable() const { return dumpable_ && !managedService_ && uid_ == effectiveUid_ && gid_ == effectiveGid_; }
    uint64_t coreLimit() const { return coreLimit_; }
    uint64_t coreHardLimit() const { return coreHardLimit_; }
    bool setCoreLimit(uint64_t soft, uint64_t hard);
    int resourceLimit(Task& caller, int resource, bool set, uint64_t& soft, uint64_t& hard);
    int32_t mainNice();
    int32_t initialNice() const { return initialNice_; }
    uint64_t mainThreadId();
    uint32_t threadCount();
    bool threadInfo(uint64_t minimumTid, os::wire::ThreadInfo& info);
    void credentials(os::wire::CredentialInfo& info);
    int changeCredentials(unsigned operation, uint32_t first, uint32_t second, uint32_t third, const uint32_t* groups = nullptr);
    bool recordImage(const os::wire::ImageMapping& mapping);
    void recordAuxiliary(uint64_t headers, uint64_t count, uint64_t entry, uint64_t loader);
    bool inheritForFork(Task& parent);
    bool commitExec(uint64_t newRootPhysicalAddress, const char* name,
                    Thread* currentThread);
    int waitForChild(int64_t taskId, Thread* waiter, int& status,
                     uint32_t options, uint64_t& childId, os::wire::ChildEvent* event = nullptr);
    void notifyChildStateChange();
    bool attachThread(Thread* thread);
    void retireJoinedThread(Thread* thread);
    int waitForThread(uint64_t threadId, Thread* waiter, int& exitCode,
                      bool noHang);
    void notifyThreadExit(Thread* thread);
    void destroy();
    void terminateOtherThreads(int exitCode);
    void grantCapabilities(uint64_t capabilities);
    bool hasCapability(uint64_t capability) const;
    bool isSystemService() const { return systemService_; }
    void setSystemService(bool value) { systemService_ = value; }
    os::ServiceRole serviceRole() const { return serviceRole_; }
    void setServiceRole(os::ServiceRole role) { serviceRole_ = role; if (role != os::ServiceRole::None) managedService_ = true; }
    bool managedService() const { return managedService_; }
    void setManagedService(bool value) { managedService_ = value; }
    uint32_t uid() const { return uid_; }
    uint32_t gid() const { return gid_; }
    uint32_t effectiveUid() const { return effectiveUid_; }
    uint32_t effectiveGid() const { return effectiveGid_; }
    void setCredentials(uint32_t uid, uint32_t gid, bool effectiveOnly = false);
    uint32_t terminalId() const { return terminalId_; }
    void setTerminalId(uint32_t value) { terminalId_ = value; }
    uint32_t terminalKind() const { return terminalKind_; }
    uint32_t terminalIndex() const { return terminalId_; }
    void setTerminal(uint32_t kind, uint32_t index) { terminalKind_ = kind; terminalId_ = index; }
    uint64_t startedNanoseconds() const { return startedNanoseconds_; }
    uint64_t cpuNanoseconds() const { return __atomic_load_n(&cpuNanoseconds_, __ATOMIC_RELAXED); }
    void accountCpu(uint64_t elapsed) { __atomic_fetch_add(&cpuNanoseconds_, elapsed, __ATOMIC_RELAXED); }
    const char* commandLine() const { return commandLine_; }
    uint32_t commandLineSize() const { return commandLineSize_; }
    void setCommandLine(uint32_t count, const char* const* arguments);
    void setBootstrapHandle(uint64_t handle);
    uint64_t bootstrapHandle() const;
    uint64_t kernelHandle() const;
    void setKernelHandle(uint64_t handle);
    void setStandardHandle(uint32_t index, uint64_t handle);
    uint64_t standardHandle(uint32_t index) const;
    uint64_t taskGroupId() const;
    uint64_t sessionId() const;
    bool setTaskGroup(uint64_t taskGroupId);
    bool createSession();
    bool deliverSignal(uint32_t signal);
    bool deliverThreadSignal(uint64_t threadId, uint32_t signal);
    bool setSignalAction(uint32_t signal, const SignalAction& action,
                         SignalAction* oldAction);
    bool getSignalAction(uint32_t signal, SignalAction& action);
    uint64_t pendingSignals(Thread* thread);
    uint32_t takePendingSignal(Thread* thread, uint64_t accepted,
                               SignalAction& action, bool respectMask = true);

   private:
    uint32_t uid_ = 0, gid_ = 0, effectiveUid_ = 0, effectiveGid_ = 0;
    uint32_t savedUid_ = 0, savedGid_ = 0, supplementaryCount_ = 0;
    uint32_t supplementaryGroups_[os::maximumSupplementaryGroups]{};
    uint32_t terminalId_ = 0;
    uint32_t terminalKind_ = 0;
    uint64_t startedNanoseconds_ = 0, cpuNanoseconds_ = 0;
    char commandLine_[384]{};
    uint32_t commandLineSize_ = 0;
    bool systemService_ = false;
    bool managedService_ = false;
    os::ServiceRole serviceRole_ = os::ServiceRole::None;
    friend void task::init();
    friend class TaskReclaimer;
    friend class CoreDump;
    friend class TaskPriorities;
    friend class TaskLifecycleTest;
    friend TaskReference task::find(uint64_t taskId);
    friend TaskReference task::at(uint32_t index);
    friend TaskReference task::next(uint64_t minimumId);
    friend bool signal::sendSignalToTaskGroup(uint64_t taskGroupId,
                                              uint32_t signal, Task* caller);

    bool init(const char* name, uint64_t rootPhysicalAddress, Task* parent);
    void adoptChildren();
    bool destroyResources();
    void reapJoinedThreadLocked(Thread* thread);

    uint64_t id_;
    char name_[64];
    TaskState state_;
    AddressSpace addressSpace_;
    HandleTable handles_;
    Thread* mainThread_;
    Thread* threads_;
    uint64_t threadCount_;
    Task* parent_;
    Task* firstChild_;
    Task* nextSibling_;
    Thread* childWaiters_;
    int exitCode_;
    uint32_t exitSignal_ = 0;
    os::wire::CoreState coreState_ = os::wire::CoreState::None;
    uint64_t coreDeadline_ = 0;
    uint64_t coreLimit_ = 8 * 1024 * 1024, coreHardLimit_ = 8 * 1024 * 1024;
    uint64_t niceLimit_ = 0, niceHardLimit_ = 0;
    int32_t initialNice_ = 0;
    bool dumpable_ = true;
    bool exitNotified_ = false;
    FaultRecord exitFault_{};
    OsSignalInfo childSignalInfo_{};
    os::wire::ImageMapping imageMappings_[64]{};
    uint32_t imageMappingCount_ = 0;
    uint64_t auxiliary_[14]{};
    bool acceptsThreads_;
    uint64_t capabilities_;
    uint64_t bootstrapHandle_;
    uint64_t kernelHandle_ = 0;
    uint64_t standardHandles_[3];
    uint64_t taskGroupId_;
    uint64_t sessionId_;
    uint64_t authenticationSession_ = 0;
    uint32_t authenticationUid_ = 0, authenticationGid_ = 0;
    bool stopped_;
    bool memoryFrozen_ = false;
    uint64_t memoryFreezeDeadline_ = 0;
    uint32_t stopSignal_;
    bool stopReported_;
    bool continuedPending_;
    SignalAction signalActions_[maximumSignal + 1];
    uint64_t pendingSignals_;
    Task* nextGlobal_;
    SpinLock lock_;
    uint32_t references_ = 1;
    uint32_t resourceState_ = 0;
    Task* reclaimNext_ = nullptr;
};

namespace task {
void init();
Task* getCurrent();
Task* getIdle();
TaskReference find(uint64_t taskId);
[[noreturn]] void exitCurrent(int exitCode);
}  // namespace task

class RunQueue {
   public:
    static void clearRunQueue(RunQueue* queue);

    void insertNode(Thread* thread);
    void removeNode(Thread* thread);

    Thread* getRoot() const { return root; }
    Thread* setRoot(Thread* value) {
        root = value;
        return root;
    }
    Thread*& accessRoot() { return root; }
    Thread* getLeftmost() const { return leftmost; }
    Thread* setLeftmost(Thread* value) {
        leftmost = value;
        return leftmost;
    }
    Thread* getCurrentThread() const { return currentThread; }
    Thread* setCurrentThread(Thread* value) {
        currentThread = value;
        return currentThread;
    }
    Thread* getIdleThread() const { return idleThread; }
    Thread* setIdleThread(Thread* value) {
        idleThread = value;
        return idleThread;
    }
    Thread* getSleepingThreads() const { return sleepingThreads; }
    Thread* setSleepingThreads(Thread* value) {
        sleepingThreads = value;
        return sleepingThreads;
    }
    Thread*& accessSleepingThreads() { return sleepingThreads; }
    uint64_t getRunnableCount() const { return runnableCount; }
    uint64_t setRunnableCount(uint64_t value) {
        runnableCount = value;
        return runnableCount;
    }
    uint64_t getMinVruntime() const { return minVruntime; }
    uint64_t setMinVruntime(uint64_t value) {
        minVruntime = value;
        return minVruntime;
    }
    uint64_t getClock() const { return clock; }
    uint64_t setClock(uint64_t value) {
        clock = value;
        return clock;
    }
    SpinLock& getLock() { return lock; }
    const SpinLock& getLock() const { return lock; }

   private:
    static bool isRed(const Thread* node);
    static bool comesBefore(const Thread* left, const Thread* right);
    static Thread* minimum(Thread* node);
    void rotateLeft(Thread* node);
    void rotateRight(Thread* node);
    void insertFixup(Thread* node);
    void transplant(Thread* target, Thread* replacement);
    void deleteFixup(Thread* node, Thread* parent);

    Thread* root;
    Thread* leftmost;
    Thread* currentThread;
    Thread* idleThread;
    Thread* sleepingThreads;
    uint64_t runnableCount;
    uint64_t minVruntime;
    uint64_t clock;
    SpinLock lock;
};

namespace schedule {
void init();
bool isSchedulerInitialized();
bool enqueueThread(Thread* thread);
void prepareCurrentWait();
void blockCurrentThread();
void wakeThread(Thread* thread);
void finishContextSwitch();
void dequeueThread(Thread* thread);
void terminateThread(Thread* thread);
void quiesceTask(Task* task);
Thread* pickNextThread();
void schedule();
void yieldThread();
void requestReschedule();
void schedulerTick();
void sleepCurrentThread(uint64_t milliseconds);
uint32_t getSchedulerWeight(int32_t nice);

}  // namespace schedule

struct BootInfo;

class UserProgramLoader {
   public:
    static bool serviceCapabilities(os::ServiceRole role, uint64_t& capabilities);
    static ElfLoadError buildStack(
        uint64_t rootPhysicalAddress, const char* programName,
        uint32_t argumentCount, const char* const* arguments,
        uint32_t environmentCount, const char* const* environment,
        uint64_t runtimeStateAddress, uint64_t& stackPointer,
        const ElfLoadResult* image = nullptr, Task* owner = nullptr);

    static Task* startInitial(const BootInfo* bootInfo);

    static TaskReference load(os::ProgramId program, Task* parent,
                      uint64_t bootstrapHandle = 0, uint32_t argumentCount = 0,
                      const char* const* arguments = nullptr,
                      ElfLoadError* loadError      = nullptr);
    static TaskReference loadImage(uint64_t sourceAddress, size_t imageSize,
                           const char* name, Task* source, Task* parent,
                           uint64_t bootstrapHandle, uint64_t capabilities,
                           uint64_t standardInputHandle,
                           uint64_t standardOutputHandle,
                           uint64_t standardErrorHandle, uint32_t argumentCount,
                           const char* const* arguments,
                           const ElfSharedImage* sharedImages,
                           uint32_t sharedImageCount,
                           ElfLoadError* loadError = nullptr,
                             os::ServiceRole serviceRole = os::ServiceRole::None,
                             bool newSession = false, bool managedService = false,
                             uint32_t terminalKind = 0, uint32_t terminalIndex = 0);
};

}  // namespace kernel

namespace kernel {
class Task;
class CoreDump {
 public:
    static void init();
    static void request(Task& task);
    static void capture(Task& task);
    static bool pending(Task& task);
    static void query(os::wire::CorePolicy& result);
    static int configure(Task& caller, const os::wire::CorePolicy& policy);
    static int acquire(Task& caller, os::wire::CoreTicket& ticket);
    static int read(Task& caller, uint64_t ticket, uint64_t offset, void* destination, uint64_t size);
    static int finish(Task& caller, uint64_t ticket, bool success);
 private:
    static void expire();
};
}

namespace kernel {
class TaskPriorities {
 public:
    static int apply(Task& caller, Thread& current, unsigned operation,
                     unsigned which, uint64_t who, int value, int& result);
};
namespace schedule {
int changeNice(Thread& thread, int64_t value, bool relative, bool privileged, uint64_t limit, int& result);
}
}

#include "kernel/ipc.h"

namespace kernel {
class ServiceRecovery {
   public:
    static void init();
    static int call(Task& caller, const IpcMessage& request, IpcMessage& reply);
    static bool current(const Task& caller);
};
}
