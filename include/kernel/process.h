#pragma once

#include <stdint.h>

#include "kernel/spinlock.h"
#include "kernel/signal.h"
#include "kernel/thread.h"

namespace kernel {

struct Thread;

enum class ProcessState : uint8_t { Created, Running, Terminated, Stopped };

class AddressSpace {
   public:
    void initialize(uint64_t rootPhysicalAddress);
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
    Process,
    MmioRegion,
    DmaBuffer,
    InterruptBinding,
    Pipe,
    Pty,
    MessageQueue,
    SharedMemory
};

constexpr uint32_t handleRightRead   = 1U << 0;
constexpr uint32_t handleRightWrite  = 1U << 1;
constexpr uint32_t handleRightManage = 1U << 2;

struct HandleEntry {
    void* object;
    uint32_t rights;
    uint16_t type;
    uint16_t generation;
};

class HandleTable {
   public:
    bool initialize(uint32_t capacity);
    uint64_t add(void* object, HandleType type, uint32_t rights);
    bool addAt(uint64_t handle, void* object, HandleType type,
               uint32_t rights);
    void* get(uint64_t handle, HandleType type, uint32_t requiredRights);
    bool close(uint64_t handle);
    void closeAll();
    bool inspect(uint64_t handle, void** object, HandleType* type,
                 uint32_t* rights);
    bool inspectAt(uint32_t index, uint64_t* handle, void** object,
                   HandleType* type, uint32_t* rights);

   private:
    HandleEntry* entries_;
    uint32_t capacity_;
    SpinLock lock_;
};

constexpr uint64_t processCapabilityDevice = 1ULL << 0;
constexpr uint64_t processCapabilitySpawn  = 1ULL << 1;
constexpr uint64_t processCapabilityDisplay = 1ULL << 2;

class Process {
   public:
    static Process* create(const char* name, Process* parent = nullptr);

    uint64_t id() const;
    const char* name() const;
    ProcessState state();
    AddressSpace& addressSpace();
    HandleTable& handles();
    Process* parent() const;
    bool isTerminated();
    int exitCode();
    bool inheritForFork(Process& parent);
    bool commitExec(uint64_t newRootPhysicalAddress, const char* name,
                    Thread* currentThread);
    int waitForChild(int64_t processId, Thread* waiter, int& status,
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
    void setBootstrapHandle(uint64_t handle);
    uint64_t bootstrapHandle() const;
    void setStandardHandle(uint32_t index, uint64_t handle);
    uint64_t standardHandle(uint32_t index) const;
    uint64_t processGroupId() const;
    uint64_t sessionId() const;
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
    friend void initProcessSubsystem();
    friend Process* findProcess(uint64_t processId);
    friend bool sendSignalToProcessGroup(uint64_t processGroupId,
                                         uint32_t signal);

    bool initialize(const char* name, uint64_t rootPhysicalAddress,
                    Process* parent);

    uint64_t id_;
    char name_[64];
    ProcessState state_;
    AddressSpace addressSpace_;
    HandleTable handles_;
    Thread* mainThread_;
    Thread* threads_;
    uint64_t threadCount_;
    Process* parent_;
    Process* firstChild_;
    Process* nextSibling_;
    Thread* childWaiters_;
    int exitCode_;
    bool acceptsThreads_;
    uint64_t capabilities_;
    uint64_t bootstrapHandle_;
    uint64_t standardHandles_[3];
    uint64_t processGroupId_;
    uint64_t sessionId_;
    bool stopped_;
    uint32_t stopSignal_;
    bool stopReported_;
    bool continuedPending_;
    SignalAction signalActions_[maximumSignal + 1];
    uint64_t pendingSignals_;
    Process* nextGlobal_;
    SpinLock lock_;
};

void initProcessSubsystem();
Process* getCurrentProcess();
Process* getKernelProcess();
Process* findProcess(uint64_t processId);
[[noreturn]] void exitCurrentProcess(int exitCode);

}  // namespace kernel
