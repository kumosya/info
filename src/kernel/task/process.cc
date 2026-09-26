#include "kernel/process.h"

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/heap.h"
#include "kernel/ipc.h"
#include "kernel/new.h"
#include "kernel/page.h"
#include "kernel/pipe.h"
#include "kernel/mqueue.h"
#include "kernel/pty.h"
#include "kernel/scheduler.h"
#include "kernel/shared_memory.h"
#include "kernel/serial.h"
#include "kernel/signal.h"
#include "kernel/smp.h"

namespace {

constexpr uint32_t initialHandleCapacity = 64;
kernel::Atomic<uint64_t> nextProcessId{1};
kernel::Process* kernelProcess;
kernel::Process* processList;
kernel::SpinLock processListLock;

void clearMemory(void* address, uint64_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(address);
    for (uint64_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void copyName(char* destination, const char* source) {
    uint32_t index = 0;
    if (source != nullptr) {
        while (index < 63 && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }
    }
    destination[index] = '\0';
}

}  // namespace

namespace kernel {

void AddressSpace::initialize(uint64_t rootPhysicalAddress) {
    rootPhysicalAddress_ = rootPhysicalAddress;
    lock_.initialize();
}

uint64_t AddressSpace::replaceRoot(uint64_t rootPhysicalAddress) {
    const uint64_t previous = rootPhysicalAddress_;
    rootPhysicalAddress_ = rootPhysicalAddress;
    return previous;
}

uint64_t AddressSpace::rootPhysicalAddress() const {
    return rootPhysicalAddress_;
}

SpinLock& AddressSpace::lock() { return lock_; }

bool HandleTable::initialize(uint32_t capacity) {
    entries_ = static_cast<HandleEntry*>(
        kernelAllocate(sizeof(HandleEntry) * capacity));
    if (entries_ == nullptr) {
        return false;
    }
    clearMemory(entries_, sizeof(HandleEntry) * capacity);
    capacity_ = capacity;
    lock_.initialize();
    return true;
}

uint64_t HandleTable::add(void* object, HandleType type, uint32_t rights) {
    if (object == nullptr || type == HandleType::None) {
        return 0;
    }
    InterruptSpinLockGuard guard(lock_);
    for (uint32_t index = 0; index < capacity_; ++index) {
        HandleEntry& entry = entries_[index];
        if (entry.object != nullptr) {
            continue;
        }
        ++entry.generation;
        if (entry.generation == 0) {
            ++entry.generation;
        }
        entry.object = object;
        entry.rights = rights;
        entry.type   = static_cast<uint16_t>(type);
        return (static_cast<uint64_t>(entry.generation) << 32) | (index + 1);
    }
    return 0;
}

bool HandleTable::addAt(uint64_t handle, void* object, HandleType type,
                        uint32_t rights) {
    const uint32_t encodedIndex = static_cast<uint32_t>(handle);
    const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
    if (object == nullptr || type == HandleType::None || encodedIndex == 0 ||
        encodedIndex > capacity_ || generation == 0) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    HandleEntry& entry = entries_[encodedIndex - 1];
    if (entry.object != nullptr) return false;
    entry.object     = object;
    entry.rights     = rights;
    entry.type       = static_cast<uint16_t>(type);
    entry.generation = generation;
    return true;
}

void* HandleTable::get(uint64_t handle, HandleType type,
                       uint32_t requiredRights) {
    const uint32_t encodedIndex = static_cast<uint32_t>(handle);
    const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
    if (encodedIndex == 0 || encodedIndex > capacity_ || generation == 0) {
        return nullptr;
    }
    InterruptSpinLockGuard guard(lock_);
    const HandleEntry& entry = entries_[encodedIndex - 1];
    if (entry.object == nullptr || entry.generation != generation ||
        entry.type != static_cast<uint16_t>(type) ||
        (entry.rights & requiredRights) != requiredRights) {
        return nullptr;
    }
    return entry.object;
}

bool HandleTable::inspect(uint64_t handle, void** object, HandleType* type,
                          uint32_t* rights) {
    const uint32_t encodedIndex = static_cast<uint32_t>(handle);
    const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
    if (object == nullptr || type == nullptr || rights == nullptr ||
        encodedIndex == 0 || encodedIndex > capacity_ || generation == 0) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    const HandleEntry& entry = entries_[encodedIndex - 1];
    if (entry.object == nullptr || entry.generation != generation) {
        return false;
    }
    *object = entry.object;
    *type   = static_cast<HandleType>(entry.type);
    *rights = entry.rights;
    return true;
}

bool HandleTable::inspectAt(uint32_t index, uint64_t* handle, void** object,
                            HandleType* type, uint32_t* rights) {
    if (handle == nullptr || object == nullptr || type == nullptr ||
        rights == nullptr || index >= capacity_) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    const HandleEntry& entry = entries_[index];
    if (entry.object == nullptr || entry.generation == 0) return false;
    *handle = (static_cast<uint64_t>(entry.generation) << 32) | (index + 1);
    *object = entry.object;
    *type   = static_cast<HandleType>(entry.type);
    *rights = entry.rights;
    return true;
}

bool HandleTable::close(uint64_t handle) {
    const uint32_t encodedIndex = static_cast<uint32_t>(handle);
    const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
    if (encodedIndex == 0 || encodedIndex > capacity_ || generation == 0) {
        return false;
    }
    void* object = nullptr;
    HandleType type = HandleType::None;
    {
        InterruptSpinLockGuard guard(lock_);
        HandleEntry& entry = entries_[encodedIndex - 1];
        if (entry.object == nullptr || entry.generation != generation) {
            return false;
        }
        object = entry.object;
        type = static_cast<HandleType>(entry.type);
        entry.object = nullptr;
        entry.rights = 0;
        entry.type   = static_cast<uint16_t>(HandleType::None);
    }
    if (type == HandleType::Pipe) {
        closePipeHandleObject(object);
    } else if (type == HandleType::Pty) {
        closePtyHandleObject(object);
    } else if (type == HandleType::MessageQueue) {
        closeMessageQueueHandleObject(object);
    } else if (type == HandleType::SharedMemory) {
        closeSharedMemoryHandleObject(object);
    }
    return true;
}

void HandleTable::closeAll() {
    for (uint32_t index = 0; index < capacity_; ++index) {
        uint64_t handle = 0;
        {
            InterruptSpinLockGuard guard(lock_);
            const HandleEntry& entry = entries_[index];
            if (entry.object != nullptr) {
                handle = (static_cast<uint64_t>(entry.generation) << 32) |
                         (index + 1);
            }
        }
        if (handle != 0) {
            close(handle);
        }
    }
}

bool Process::initialize(const char* name, uint64_t rootPhysicalAddress,
                         Process* parent) {
    id_ = nextProcessId.fetchAdd(1, MemoryOrder::Relaxed);
    copyName(name_, name);
    state_ = ProcessState::Created;
    addressSpace_.initialize(rootPhysicalAddress);
    if (!handles_.initialize(initialHandleCapacity)) {
        return false;
    }
    mainThread_      = nullptr;
    threads_         = nullptr;
    threadCount_     = 0;
    parent_          = parent;
    firstChild_      = nullptr;
    nextSibling_     = nullptr;
    childWaiters_    = nullptr;
    exitCode_        = 0;
    acceptsThreads_  = true;
    capabilities_    = 0;
    bootstrapHandle_ = 0;
    clearMemory(standardHandles_, sizeof(standardHandles_));
    processGroupId_ = parent == nullptr ? id_ : parent->processGroupId();
    sessionId_ = parent == nullptr ? id_ : parent->sessionId();
    stopped_ = false;
    stopSignal_ = 0;
    stopReported_ = false;
    continuedPending_ = false;
    clearMemory(signalActions_, sizeof(signalActions_));
    pendingSignals_ = 0;
    nextGlobal_ = nullptr;
    lock_.initialize();
    return true;
}

Process* Process::create(const char* name, Process* parent) {
    const uint64_t root = createAddressSpaceRoot();
    if (root == 0) {
        return nullptr;
    }
    void* storage    = kernelAllocate(sizeof(Process));
    Process* process = storage == nullptr ? nullptr : new (storage) Process;
    if (process == nullptr || !process->initialize(name, root, parent)) {
        destroyAddressSpaceRoot(root);
        return nullptr;
    }
    if (parent != nullptr) {
        InterruptSpinLockGuard guard(parent->lock_);
        process->nextSibling_ = parent->firstChild_;
        parent->firstChild_   = process;
    }
    {
        InterruptSpinLockGuard guard(processListLock);
        process->nextGlobal_ = processList;
        processList = process;
    }
    return process;
}

uint64_t Process::id() const { return id_; }

const char* Process::name() const { return name_; }

ProcessState Process::state() {
    InterruptSpinLockGuard guard(lock_);
    return stopped_ && state_ == ProcessState::Running ? ProcessState::Stopped
                                                       : state_;
}

AddressSpace& Process::addressSpace() { return addressSpace_; }

HandleTable& Process::handles() { return handles_; }

Process* Process::parent() const { return parent_; }

bool Process::isTerminated() { return state() == ProcessState::Terminated; }

int Process::exitCode() {
    InterruptSpinLockGuard guard(lock_);
    return exitCode_;
}

bool Process::inheritForFork(Process& parent) {
    {
        InterruptSpinLockGuard parentGuard(parent.lock_);
        InterruptSpinLockGuard childGuard(lock_);
        capabilities_    = parent.capabilities_;
        bootstrapHandle_ = parent.bootstrapHandle_;
        for (uint32_t index = 0; index < 3; ++index) {
            standardHandles_[index] = parent.standardHandles_[index];
        }
        processGroupId_ = parent.processGroupId_;
        sessionId_      = parent.sessionId_;
        for (uint32_t signal = 0; signal <= maximumSignal; ++signal) {
            signalActions_[signal] = parent.signalActions_[signal];
        }
        pendingSignals_ = 0;
    }

    for (uint32_t index = 0; index < initialHandleCapacity; ++index) {
        uint64_t handle = 0;
        void* object = nullptr;
        HandleType type = HandleType::None;
        uint32_t rights = 0;
        if (!parent.handles_.inspectAt(index, &handle, &object, &type,
                                       &rights)) {
            continue;
        }
        uint64_t inherited = 0;
        if (type == HandleType::Endpoint) {
            inherited = duplicateEndpointHandle(&parent, this, handle, handle);
        } else if (type == HandleType::Pipe) {
            inherited = duplicatePipeHandle(parent, *this, handle, handle);
        } else if (type == HandleType::Pty) {
            inherited = duplicatePtyHandle(parent, *this, handle, handle);
        } else if (type == HandleType::MessageQueue) {
            inherited = duplicateMessageQueueHandle(parent, *this, handle,
                                                     handle);
        } else if (type == HandleType::SharedMemory) {
            inherited = duplicateSharedMemoryHandle(parent, *this, handle,
                                                     handle);
        } else if (handles_.addAt(handle, object, type, rights)) {
            inherited = handle;
        }
        if (inherited != handle) {
            handles_.closeAll();
            return false;
        }
    }
    return true;
}

bool Process::commitExec(uint64_t newRootPhysicalAddress, const char* name,
                         Thread* currentThread) {
    if (newRootPhysicalAddress == 0 || currentThread == nullptr ||
        currentThread->process != this || this == kernelProcess) {
        return false;
    }
    uint64_t oldRoot = 0;
    {
        InterruptSpinLockGuard guard(lock_);
        for (Thread* thread = threads_; thread != nullptr;) {
            Thread* next = thread->processNext;
            if (thread != currentThread &&
                thread->state != ThreadState::Terminated) {
                dequeueThread(thread);
                thread->state = ThreadState::Terminated;
                requestRemoteReschedule(static_cast<uint32_t>(thread->cpuId));
            }
            thread = next;
        }
        currentThread->processNext = nullptr;
        currentThread->pendingSignals = 0;
        currentThread->waitNext = nullptr;
        threads_ = currentThread;
        mainThread_ = currentThread;
        threadCount_ = 1;
        acceptsThreads_ = true;
        state_ = ProcessState::Running;
        stopped_ = false;
        stopSignal_ = 0;
        stopReported_ = false;
        continuedPending_ = false;
        pendingSignals_ = 0;
        for (uint32_t signal = 1; signal <= maximumSignal; ++signal) {
            if (signalActions_[signal].handler != signalIgnore) {
                signalActions_[signal] = {};
            }
        }
        copyName(name_, name);
        oldRoot = addressSpace_.replaceRoot(newRootPhysicalAddress);
    }
    releaseProcessSharedMemoryMappings(*this);
    switchAddressSpace(newRootPhysicalAddress);
    destroyAddressSpaceRoot(oldRoot);
    return true;
}

int Process::waitForChild(int64_t processId, Thread* waiter, int& status,
                          uint32_t options, uint64_t& childId) {
    if (waiter == nullptr) return -1;
    Process* reaped = nullptr;
    bool found = false;
    {
        InterruptSpinLockGuard guard(lock_);
        Process** link = &firstChild_;
        while (*link != nullptr) {
            Process* child = *link;
            const bool matches =
                processId == -1 ||
                (processId > 0 && child->id_ == static_cast<uint64_t>(processId)) ||
                (processId == 0 && child->processGroupId_ == processGroupId_) ||
                (processId < -1 &&
                 child->processGroupId_ == static_cast<uint64_t>(-processId));
            if (!matches) {
                link = &child->nextSibling_;
                continue;
            }
            found = true;
            bool terminated = false;
            bool stateEvent = false;
            {
                InterruptSpinLockGuard childGuard(child->lock_);
                terminated = child->state_ == ProcessState::Terminated;
                if (terminated) {
                    status = child->exitCode_ >= 0
                                 ? (child->exitCode_ & 0xff) << 8
                                 : ((-child->exitCode_) & 0x7f);
                    stateEvent = true;
                } else if ((options & 2U) != 0 && child->stopped_ &&
                           !child->stopReported_) {
                    child->stopReported_ = true;
                    status = (static_cast<int>(child->stopSignal_) << 8) |
                             0x7f;
                    stateEvent = true;
                } else if ((options & 8U) != 0 &&
                           child->continuedPending_) {
                    child->continuedPending_ = false;
                    status = 0xffff;
                    stateEvent = true;
                }
            }
            if (terminated) {
                *link = child->nextSibling_;
                child->nextSibling_ = nullptr;
                reaped = child;
                childId = child->id_;
                break;
            }
            if (stateEvent) {
                childId = child->id_;
                return 0;
            }
            link = &child->nextSibling_;
        }
        if (reaped == nullptr && found && (options & 1U) == 0) {
            waiter->waitNext = childWaiters_;
            childWaiters_ = waiter;
        }
    }
    if (reaped != nullptr) {
        reaped->destroy();
        InterruptSpinLockGuard globalGuard(processListLock);
        Process** link = &processList;
        while (*link != nullptr && *link != reaped) {
            link = &(*link)->nextGlobal_;
        }
        if (*link == reaped) *link = reaped->nextGlobal_;
        reaped->nextGlobal_ = nullptr;
        return 0;
    }
    if (!found) return -1;
    if ((options & 1U) != 0) {
        childId = 0;
        status = 0;
        return 2;
    }
    return 1;
}

void Process::notifyChildStateChange() {
    Thread* waiters = nullptr;
    {
        InterruptSpinLockGuard guard(lock_);
        waiters = childWaiters_;
        childWaiters_ = nullptr;
    }
    while (waiters != nullptr) {
        Thread* waiter = waiters;
        waiters = waiters->waitNext;
        waiter->waitNext = nullptr;
        wakeThread(waiter);
    }
}

bool Process::attachThread(Thread* thread) {
    if (thread == nullptr) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    if (!acceptsThreads_) {
        return false;
    }
    thread->process     = this;
    thread->processNext = threads_;
    threads_            = thread;
    ++threadCount_;
    if (mainThread_ == nullptr) {
        mainThread_ = thread;
    }
    state_ = ProcessState::Running;
    return true;
}

int Process::waitForThread(uint64_t threadId, Thread* waiter, int& exitCode,
                           bool noHang) {
    if (waiter == nullptr || waiter->id == threadId) {
        return -1;
    }
    InterruptSpinLockGuard guard(lock_);
    Thread* target = threads_;
    while (target != nullptr && target->id != threadId) {
        target = target->processNext;
    }
    if (target == nullptr) {
        return -1;
    }
    if (target->state == ThreadState::Terminated) {
        exitCode = target->exitCode;
        return 0;
    }
    if (noHang) {
        exitCode = 0;
        return 2;
    }
    waiter->waitNext = nullptr;
    if (target->joinWaitersTail == nullptr) {
        target->joinWaiters = waiter;
    } else {
        target->joinWaitersTail->waitNext = waiter;
    }
    target->joinWaitersTail = waiter;
    exitCode = 0;
    return 1;
}

void Process::notifyThreadExit(Thread* thread) {
    Thread* waiters = nullptr;
    bool processTerminated = false;
    {
        InterruptSpinLockGuard guard(lock_);
        waiters = thread->joinWaiters;
        thread->joinWaiters = nullptr;
        thread->joinWaitersTail = nullptr;
        if (threadCount_ != 0) {
            --threadCount_;
        }
        if (threadCount_ == 0 && this != kernelProcess) {
            state_          = ProcessState::Terminated;
            acceptsThreads_ = false;
            exitCode_       = thread->exitCode;
            handles_.closeAll();
            processTerminated = true;
        }
    }
    while (waiters != nullptr) {
        Thread* waiter = waiters;
        waiters = waiters->waitNext;
        waiter->waitNext = nullptr;
        wakeThread(waiter);
    }
    if (processTerminated && parent_ != nullptr) {
        parent_->notifyChildStateChange();
        parent_->deliverSignal(static_cast<uint32_t>(Signal::Child));
    }
}

void Process::terminateOtherThreads(int exitCode) {
    InterruptSpinLockGuard guard(lock_);
    acceptsThreads_ = false;
    exitCode_       = exitCode;
    for (Thread* thread = threads_; thread != nullptr;
         thread         = thread->processNext) {
        if (thread != getCurrentThread() &&
            thread->state != ThreadState::Terminated) {
            dequeueThread(thread);
            thread->state = ThreadState::Terminated;
            requestRemoteReschedule(static_cast<uint32_t>(thread->cpuId));
            if (threadCount_ != 0) {
                --threadCount_;
            }
        }
    }
    if (threadCount_ == 0) {
        state_ = ProcessState::Terminated;
    }
}

void Process::destroy() {
    if (this == kernelProcess || this == getCurrentProcess()) {
        return;
    }
    uint64_t root;
    bool discard;
    {
        InterruptSpinLockGuard guard(lock_);
        discard = state_ == ProcessState::Created && threadCount_ == 0;
        acceptsThreads_ = false;
        for (Thread* thread = threads_; thread != nullptr;
             thread         = thread->processNext) {
            if (thread->state != ThreadState::Terminated) {
                dequeueThread(thread);
                thread->state = ThreadState::Terminated;
            }
        }
        threadCount_ = 0;
        state_       = ProcessState::Terminated;
        root         = addressSpace_.rootPhysicalAddress();
        addressSpace_.initialize(0);
    }
    handles_.closeAll();
    releaseProcessSharedMemoryMappings(*this);
    if (root != 0) destroyAddressSpaceRoot(root);
    if (discard) {
        if (parent_ != nullptr) {
            InterruptSpinLockGuard parentGuard(parent_->lock_);
            Process** link = &parent_->firstChild_;
            while (*link != nullptr && *link != this) {
                link = &(*link)->nextSibling_;
            }
            if (*link == this) *link = nextSibling_;
            nextSibling_ = nullptr;
        }
        InterruptSpinLockGuard globalGuard(processListLock);
        Process** link = &processList;
        while (*link != nullptr && *link != this) {
            link = &(*link)->nextGlobal_;
        }
        if (*link == this) *link = nextGlobal_;
        nextGlobal_ = nullptr;
    }
}

void Process::grantCapabilities(uint64_t capabilities) {
    capabilities_ |= capabilities;
}

bool Process::hasCapability(uint64_t capability) const {
    return (capabilities_ & capability) == capability;
}

void Process::setBootstrapHandle(uint64_t handle) { bootstrapHandle_ = handle; }

uint64_t Process::bootstrapHandle() const { return bootstrapHandle_; }

void Process::setStandardHandle(uint32_t index, uint64_t handle) {
    if (index < 3) {
        standardHandles_[index] = handle;
    }
}

uint64_t Process::standardHandle(uint32_t index) const {
    return index < 3 ? standardHandles_[index] : 0;
}

uint64_t Process::processGroupId() const { return processGroupId_; }

uint64_t Process::sessionId() const { return sessionId_; }

bool Process::createSession() {
    InterruptSpinLockGuard guard(lock_);
    if (processGroupId_ == id_) {
        return false;
    }
    sessionId_ = id_;
    processGroupId_ = id_;
    return true;
}

bool Process::setSignalAction(uint32_t signal, const SignalAction& action,
                              SignalAction* oldAction) {
    if (!isValidSignal(signal) || signal == static_cast<uint32_t>(Signal::Kill) ||
        signal == static_cast<uint32_t>(Signal::Stop)) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    if (oldAction != nullptr) *oldAction = signalActions_[signal];
    signalActions_[signal] = action;
    if (action.handler == signalIgnore) {
        pendingSignals_ &= ~(1ULL << (signal - 1));
    }
    return true;
}

bool Process::getSignalAction(uint32_t signal, SignalAction& action) {
    if (!isValidSignal(signal)) return false;
    InterruptSpinLockGuard guard(lock_);
    action = signalActions_[signal];
    return true;
}

uint64_t Process::pendingSignals(Thread* thread) {
    InterruptSpinLockGuard guard(lock_);
    return pendingSignals_ | (thread == nullptr ? 0 : thread->pendingSignals);
}

uint32_t Process::takePendingSignal(Thread* thread, uint64_t accepted,
                                    SignalAction& action, bool respectMask) {
    if (thread == nullptr) return 0;
    InterruptSpinLockGuard guard(lock_);
    uint64_t available = (pendingSignals_ | thread->pendingSignals) & accepted;
    if (respectMask) available &= ~thread->signalMask;
    if (available == 0) return 0;
    const uint32_t signal = static_cast<uint32_t>(__builtin_ctzll(available)) + 1;
    const uint64_t bit = 1ULL << (signal - 1);
    if ((thread->pendingSignals & bit) != 0) thread->pendingSignals &= ~bit;
    else pendingSignals_ &= ~bit;
    action = signalActions_[signal];
    if ((action.flags & signalFlagResetHand) != 0) {
        signalActions_[signal] = {};
    }
    return signal;
}

bool Process::deliverThreadSignal(uint64_t threadId, uint32_t signal) {
    if (signal > maximumSignal || this == kernelProcess || isTerminated()) {
        return false;
    }
    {
        InterruptSpinLockGuard guard(lock_);
        Thread* target = threads_;
        while (target != nullptr && target->id != threadId) {
            target = target->processNext;
        }
        if (target == nullptr || target->state == ThreadState::Terminated) {
            return false;
        }
        if (signal == 0) return true;
        const SignalAction action = signalActions_[signal];
        if (action.handler == signalIgnore) return true;
        if (action.handler != signalDefault) {
            target->pendingSignals |= 1ULL << (signal - 1);
            return true;
        }
    }
    return deliverSignal(signal);
}

bool Process::deliverSignal(uint32_t signal) {
    if (!isValidSignal(signal) || this == kernelProcess || isTerminated()) {
        return false;
    }
    SignalAction action{};
    bool queued = false;
    {
        InterruptSpinLockGuard guard(lock_);
        action = signalActions_[signal];
        if (action.handler == signalIgnore) return true;
        bool allBlocked = true;
        for (Thread* thread = threads_; thread != nullptr;
             thread = thread->processNext) {
            if (thread->state != ThreadState::Terminated &&
                (thread->signalMask & (1ULL << (signal - 1))) == 0) {
                allBlocked = false;
                break;
            }
        }
        queued = signal != static_cast<uint32_t>(Signal::Kill) &&
                 signal != static_cast<uint32_t>(Signal::Stop) &&
                 (action.handler != signalDefault || allBlocked);
        if (queued) {
            pendingSignals_ |= 1ULL << (signal - 1);
        }
    }
    if (queued) return true;
    if (signal == static_cast<uint32_t>(Signal::WindowChanged) ||
        signal == static_cast<uint32_t>(Signal::Child) ||
        signal == static_cast<uint32_t>(Signal::Urgent)) return true;
    if (signal == static_cast<uint32_t>(Signal::Continue)) {
        {
            InterruptSpinLockGuard guard(lock_);
            stopped_ = false;
            continuedPending_ = true;
            for (Thread* thread = threads_; thread != nullptr;
                 thread = thread->processNext) {
                if (thread->state != ThreadState::Stopped) continue;
                thread->state = thread->stateBeforeStop;
                if (thread->state == ThreadState::Ready ||
                    thread->state == ThreadState::Running) {
                    thread->state = ThreadState::Ready;
                    enqueueThread(thread);
                }
            }
        }
        if (parent_ != nullptr) {
            parent_->notifyChildStateChange();
            parent_->deliverSignal(static_cast<uint32_t>(Signal::Child));
        }
        return true;
    }
    if (signal == static_cast<uint32_t>(Signal::Stop) ||
        signal == static_cast<uint32_t>(Signal::TerminalStop) ||
        signal == static_cast<uint32_t>(Signal::TerminalInput) ||
        signal == static_cast<uint32_t>(Signal::TerminalOutput)) {
        {
            InterruptSpinLockGuard guard(lock_);
            stopped_ = true;
            stopSignal_ = signal;
            stopReported_ = false;
            continuedPending_ = false;
            for (Thread* thread = threads_; thread != nullptr;
                 thread = thread->processNext) {
                if (thread->state == ThreadState::Terminated ||
                    thread->state == ThreadState::Stopped) continue;
                thread->stateBeforeStop = thread->state;
                dequeueThread(thread);
                thread->state = ThreadState::Stopped;
                requestRemoteReschedule(static_cast<uint32_t>(thread->cpuId));
            }
        }
        if (parent_ != nullptr) {
            parent_->notifyChildStateChange();
            parent_->deliverSignal(static_cast<uint32_t>(Signal::Child));
        }
        return true;
    }
    terminateOtherThreads(-static_cast<int>(signal));
    if (this == getCurrentProcess()) {
        exitThread(-static_cast<int>(signal));
    }
    destroy();
    return true;
}

void initProcessSubsystem() {
    processListLock.initialize();
    processList = nullptr;
    void* storage    = kernelAllocate(sizeof(Process));
    Process* process = storage == nullptr ? nullptr : new (storage) Process;
    if (process == nullptr ||
        !process->initialize("kernel", getKernelAddressSpaceRoot(), nullptr)) {
        serialWrite("[PROCESS] unable to initialize kernel process\n");
        haltCpu();
    }
    kernelProcess = process;
    processList = process;
    kernelProcess->attachThread(getCurrentThread());
    getCurrentCpu()->currentProcess = kernelProcess;
    serialWrite("[PROCESS] kernel process initialized\n");
}

Process* getCurrentProcess() { return getCurrentCpu()->currentProcess; }

Process* getKernelProcess() { return kernelProcess; }

Process* findProcess(uint64_t processId) {
    InterruptSpinLockGuard guard(processListLock);
    for (Process* process = processList; process != nullptr;
         process = process->nextGlobal_) {
        if (process->id() == processId) return process;
    }
    return nullptr;
}

bool isValidSignal(uint32_t signal) {
    return signal >= 1 && signal <= maximumSignal;
}

bool sendSignalToProcess(uint64_t processId, uint32_t signal) {
    Process* process = findProcess(processId);
    return process != nullptr && process->deliverSignal(signal);
}

bool sendSignalToProcessGroup(uint64_t processGroupId, uint32_t signal) {
    if (processGroupId == 0 || !isValidSignal(signal)) return false;
    bool delivered = false;
    InterruptSpinLockGuard guard(processListLock);
    for (Process* process = processList; process != nullptr;
         process = process->nextGlobal_) {
        if (process->processGroupId() == processGroupId) {
            delivered = process->deliverSignal(signal) || delivered;
        }
    }
    return delivered;
}

[[noreturn]] void exitCurrentProcess(int exitCode) {
    Process* process = getCurrentProcess();
    if (process == nullptr || process == kernelProcess) {
        exitThread(exitCode);
    }
    process->terminateOtherThreads(exitCode);
    exitThread(exitCode);
}

}  // namespace kernel
