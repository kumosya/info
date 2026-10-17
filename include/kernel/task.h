#pragma once

#include <stdint.h>

#include "kernel/elf.h"
#include "kernel/signal.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"
#include "os/abi.h"

namespace kernel {

class Thread;
class Task;
namespace posix { class FileTable; }
namespace task {
void init();
Task* find(uint64_t taskId);
Task* at(uint32_t index);
}  // namespace task

enum class TaskState : uint8_t { Created, Running, Terminated, Stopped };

class AddressSpace {
   public:
    void init(uint64_t rootPhysicalAddress);
    uint64_t replaceRoot(uint64_t rootPhysicalAddress);
    uint64_t rootPhysicalAddress() const;
    SpinLock& lock();

   private:
    uint64_t rootPhysicalAddress_;
    SpinLock lock_;
};

enum class HandleType : uint16_t {
    None,
    Endpoint,
    Task,
    MmioRegion,
    DmaBuffer,
    InterruptBinding,
    Pipe,
    Channel,
    MessageQueue,
    SharedMemory
};

constexpr uint32_t handleRightRead   = 1U << 0;
constexpr uint32_t handleRightWrite  = 1U << 1;
constexpr uint32_t handleRightManage = 1U << 2;

class HandleTable {
   public:
    uint32_t capacity();
    bool init(uint32_t capacity);
    uint64_t add(void* object, HandleType type, uint32_t rights);
    bool addAt(uint64_t handle, void* object, HandleType type, uint32_t rights);
    void* get(uint64_t handle, HandleType type, uint32_t requiredRights);
    bool close(uint64_t handle);
    void closeAll();
    bool inspect(uint64_t handle, void** object, HandleType* type,
                 uint32_t* rights);
    bool inspectAt(uint32_t index, uint64_t* handle, void** object,
                   HandleType* type, uint32_t* rights);

   private:
    bool growLocked(uint32_t required);
    class HandleEntry {
       public:
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
};

constexpr uint64_t taskCapabilityDevice  = 1ULL << 0;
constexpr uint64_t taskCapabilitySpawn   = 1ULL << 1;
constexpr uint64_t taskCapabilityDisplay = 1ULL << 2;
constexpr uint64_t taskCapabilityUsers = 1ULL << 3;
constexpr uint64_t taskCapabilityPci = 1ULL << 4;

class Task {
   public:
    static Task* create(const char* name, Task* parent = nullptr);

    uint64_t id() const;
    const char* name() const;
    TaskState state();
    AddressSpace& addressSpace();
    HandleTable& handles();
    Task* parent() const;
    bool isTerminated();
    int exitCode();
    bool inheritForFork(Task& parent);
    bool commitExec(uint64_t newRootPhysicalAddress, const char* name,
                    Thread* currentThread);
    int waitForChild(int64_t taskId, Thread* waiter, int& status,
                     uint32_t options, uint64_t& childId);
    void notifyChildStateChange();
    bool attachThread(Thread* thread);
    int waitForThread(uint64_t threadId, Thread* waiter, int& exitCode,
                      bool noHang);
    void notifyThreadExit(Thread* thread);
    void destroy();
    void terminateOtherThreads(int exitCode);
    void grantCapabilities(uint64_t capabilities);
    bool hasCapability(uint64_t capability) const;
    bool isSystemService() const { return systemService_; }
    void setSystemService(bool value) { systemService_ = value; }
    uint32_t uid() const { return uid_; }
    uint32_t gid() const { return gid_; }
    uint32_t effectiveUid() const { return effectiveUid_; }
    uint32_t effectiveGid() const { return effectiveGid_; }
    void setCredentials(uint32_t uid, uint32_t gid, bool effectiveOnly = false);
    posix::FileTable* files();
    uint32_t terminalId() const { return terminalId_; }
    void setTerminalId(uint32_t value) { terminalId_ = value; }
    void setBootstrapHandle(uint64_t handle);
    uint64_t bootstrapHandle() const;
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
    uint32_t terminalId_ = 0;
    bool systemService_ = false;
    posix::FileTable* fileTable_ = nullptr;
    friend void task::init();
    friend Task* task::find(uint64_t taskId);
    friend Task* task::at(uint32_t index);
    friend bool signal::sendSignalToTaskGroup(uint64_t taskGroupId,
                                              uint32_t signal);

    bool init(const char* name, uint64_t rootPhysicalAddress, Task* parent);

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
    bool acceptsThreads_;
    uint64_t capabilities_;
    uint64_t bootstrapHandle_;
    uint64_t standardHandles_[3];
    uint64_t taskGroupId_;
    uint64_t sessionId_;
    bool stopped_;
    uint32_t stopSignal_;
    bool stopReported_;
    bool continuedPending_;
    SignalAction signalActions_[maximumSignal + 1];
    uint64_t pendingSignals_;
    Task* nextGlobal_;
    SpinLock lock_;
};

namespace task {
void init();
Task* getCurrent();
Task* getIdle();
Task* find(uint64_t taskId);
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
void enqueueThread(Thread* thread);
void prepareCurrentWait();
void blockCurrentThread();
void wakeThread(Thread* thread);
void finishContextSwitch();
void dequeueThread(Thread* thread);
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
    static ElfLoadError buildStack(
        uint64_t rootPhysicalAddress, const char* programName,
        uint32_t argumentCount, const char* const* arguments,
        uint32_t environmentCount, const char* const* environment,
        uint64_t runtimeStateAddress, uint64_t& stackPointer);

    static Task* startInitial(const BootInfo* bootInfo);

    static Task* load(os::ProgramId program, Task* parent,
                      uint64_t bootstrapHandle = 0, uint32_t argumentCount = 0,
                      const char* const* arguments = nullptr,
                      ElfLoadError* loadError      = nullptr);
    static Task* loadImage(uint64_t sourceAddress, size_t imageSize,
                           const char* name, Task* source, Task* parent,
                           uint64_t bootstrapHandle, uint64_t capabilities,
                           uint64_t standardInputHandle,
                           uint64_t standardOutputHandle,
                           uint64_t standardErrorHandle, uint32_t argumentCount,
                           const char* const* arguments,
                           const ElfSharedImage* sharedImages,
                           uint32_t sharedImageCount,
                           ElfLoadError* loadError = nullptr);
};

}  // namespace kernel
