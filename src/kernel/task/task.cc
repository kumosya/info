/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/task.cc
 * Process identity, ancestry, resource inheritance, and image replacement.
 */

#include "kernel/task.h"

#include <new>

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"

namespace kernel::task {

namespace {

constexpr uint32_t initialHandleCapacity = 64;
::Atomic<uint64_t> nextTaskId{1};
Task* kernelTask;
Task* initTask;
Task* taskList;
SpinLock taskListLock;
uint64_t terminalForegroundGroups[2][8]{};

// Copy a task name, ensuring it's null-terminated.
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

// Retain the object and register its release with the current thread before
// restoring interrupts. ThreadCleanup also runs if the thread is killed
// without normal C++ stack unwinding; the reference does not keep the task's
// old address space alive after exec or resource reclamation.
TaskReference::TaskReference(Task* task) : task_(task) {
    const uint64_t flags = cpu::disableInterrupts();
    if (task_) {
        task_->retain();
        cleanup_.attach(
            task_, [](void* value) { static_cast<Task*>(value)->release(); });
    }
    cpu::restoreInterrupts(flags);
}
// Move the cleanup registration rather than adding a second reference. Detach
// must not release the object while ownership is being transferred.
TaskReference::TaskReference(TaskReference&& other) : task_(other.task_) {
    const uint64_t flags = cpu::disableInterrupts();
    other.cleanup_.detach();
    if (task_)
        cleanup_.attach(
            task_, [](void* value) { static_cast<Task*>(value)->release(); });
    other.task_ = nullptr;
    cpu::restoreInterrupts(flags);
}
// Release our old registration, then adopt the source registration with
// interrupts excluded. Clearing other prevents both wrappers owning it.
TaskReference& TaskReference::operator=(TaskReference&& other) {
    if (this != &other) {
        const uint64_t flags = cpu::disableInterrupts();
        cleanup_.reset();
        task_ = other.task_;
        other.cleanup_.detach();
        if (task_)
            cleanup_.attach(task_, [](void* value) {
                static_cast<Task*>(value)->release();
            });
        other.task_ = nullptr;
        cpu::restoreInterrupts(flags);
    }
    return *this;
}
// The cleanup member performs the release on normal scope exit.
TaskReference::~TaskReference() = default;
// Acquire an object-lifetime reference; this does not prevent termination.
void Task::retain() { __atomic_add_fetch(&references_, 1U, __ATOMIC_RELAXED); }
// The final reference releases this object and its retained parent. Save
// parent first because the Task storage is no longer valid after freeHeap.
void Task::release() {
    if (__atomic_sub_fetch(&references_, 1U, __ATOMIC_ACQ_REL) != 0) return;
    Task* parent = parent_;
    mm::freeHeap(this);
    if (parent) parent->release();
}
// Record the adopter used when a parent exits. The startup path installs init
// once and holds a reference independent of ordinary child ownership.
void Task::setInitTask(Task* task) {
    if (task) task->retain();
    initTask = task;
}

// Install an unpublished page-table root with the initial nonzero generation.
void AddressSpace::init(uint64_t rootPhysicalAddress) {
    rootPhysicalAddress_ = rootPhysicalAddress;
    generation_          = 1;
    residency_={};lockFuture_=false;
    lock_.init();
}

// Replace the root while the caller holds the address-space lock. Return the
// previous root, leaving CR3 switching and destruction to the caller. Bump the
// nonzero generation so old mapping objects cannot unmap a later exec image
// that happens to reuse virtual addresses.
uint64_t AddressSpace::replaceRoot(uint64_t rootPhysicalAddress) {
    const uint64_t previous = rootPhysicalAddress_;
    rootPhysicalAddress_    = rootPhysicalAddress;
    if(previous!=rootPhysicalAddress){residency_={};lockFuture_=false;}
    if (previous != rootPhysicalAddress && ++generation_ == 0) ++generation_;
    return previous;
}

// Page-table root physical address; callers arrange lifetime and locking.
uint64_t AddressSpace::rootPhysicalAddress() const {
    return rootPhysicalAddress_;
}

// Lock protecting root changes and operations on this address space.
SpinLock& AddressSpace::lock() { return lock_; }

// Task and thread creation share one atomic sequence. attachThread assigns a
// task's ID to its first thread so the main TID matches the PID.
uint64_t allocateId() { return nextTaskId.fetchAdd(1, MemoryOrder::Relaxed); }

// Initialize process state around an already allocated root. Return false if
// the initial handle table cannot be allocated; create owns root/storage
// rollback. Inherit credentials and limits under the parent lock, but start
// with no capabilities or pending signals until the caller installs them.
bool Task::init(const char* name, uint64_t rootPhysicalAddress, Task* parent) {
    id_ = task::allocateId();  // Assign a unique ID to the task
    copyName(name_, name);     // Copy the task name, ensuring null-termination
    state_ = TaskState::Created;
    addressSpace_.init(
        rootPhysicalAddress);  // Initialize the task's address space
    // with the provided root physical address
    if (!handles_.init(initialHandleCapacity, this)) {
        return false;
    }
    mainThread_  = nullptr;
    threads_     = nullptr;
    threadCount_ = 0;
    parent_      = parent;

    // To ensure the parent task remains valid while this task is active
    if (parent_) parent_->retain();

    terminalId_         = parent ? parent->terminalId() : 0;
    terminalKind_       = parent ? parent->terminalKind() : 0;
    startedNanoseconds_ = time::getMonotonicTime();
    uid_ = effectiveUid_ = parent ? parent->effectiveUid() : 0;
    gid_ = effectiveGid_ = parent ? parent->effectiveGid() : 0;
    if (parent) {
        InterruptSpinLockGuard guard(parent->lock_);
        memlockLimit_=parent->memlockLimit_;memlockHardLimit_=parent->memlockHardLimit_;
        coreLimit_          = parent->coreLimit_;
        coreHardLimit_      = parent->coreHardLimit_;
        uid_                = parent->uid_;
        effectiveUid_       = parent->effectiveUid_;
        savedUid_           = parent->savedUid_;
        gid_                = parent->gid_;
        effectiveGid_       = parent->effectiveGid_;
        savedGid_           = parent->savedGid_;
        supplementaryCount_ = parent->supplementaryCount_;
        for (uint32_t i = 0; i < supplementaryCount_; ++i)
            supplementaryGroups_[i] = parent->supplementaryGroups_[i];
        niceLimit_      = parent->niceLimit_;
        niceHardLimit_  = parent->niceHardLimit_;
        Thread* creator = currentThread();
        initialNice_    = creator && creator->getTask() == parent
                              ? creator->getNice()
                          : parent->mainThread_ ? parent->mainThread_->getNice()
                                                : 0;
    }
    if (parent)
        parent->authentication(authenticationSession_, authenticationUid_,
                               authenticationGid_);
    firstChild_      = nullptr;
    nextSibling_     = nullptr;
    childWaiters_    = nullptr;
    exitCode_        = 0;
    acceptsThreads_  = true;
    capabilities_    = 0;
    bootstrapHandle_ = 0;
    for (auto& handle : standardHandles_) handle = 0;
    taskGroupId_      = parent == nullptr ? id_ : parent->taskGroupId();
    sessionId_        = parent == nullptr ? id_ : parent->sessionId();
    stopped_          = false;
    stopSignal_       = 0;
    stopReported_     = false;
    continuedPending_ = false;
    for (auto& action : signalActions_) action = {};
    pendingSignals_ = 0;
    nextGlobal_     = nullptr;
    lock_.init();
    return true;
}

// Allocate a private root and object before publishing into the parent and
// global lists. On early failure neither list has seen the object, so the root
// and storage can be released directly. A successful object keeps a parent
// reference until final object release, including zombie lifetime.
Task* Task::create(const char* name, Task* parent) {
    const uint64_t root = mm::createAddressSpaceRoot();
    if (root == 0) {
        return nullptr;
    }
    void* storage = mm::allocateHeap(sizeof(Task));
    Task* task    = storage == nullptr ? nullptr : new (storage) Task;
    if (task == nullptr || !task->init(name, root, parent)) {
        mm::destroyAddressSpaceRoot(root);
        mm::freeHeap(storage);
        return nullptr;
    }
    mm::assignPhysicalPageOwner(root, task->id());
    if (parent != nullptr) {
        InterruptSpinLockGuard guard(parent->lock_);
        task->nextSibling_  = parent->firstChild_;
        parent->firstChild_ = task;
    }
    {
        InterruptSpinLockGuard guard(taskListLock);
        task->nextGlobal_ = taskList;
        taskList          = task;
    }
    return task;
}

// Process ID.
uint64_t Task::id() const { return id_; }

// Bounded process name.
const char* Task::name() const { return name_; }

// Expose job-control stop as a view of an otherwise Running task; freezing for
// memory recovery is a separate state with separate resumption rules.
TaskState Task::state() {
    InterruptSpinLockGuard guard(lock_);
    return stopped_ && state_ == TaskState::Running ? TaskState::Stopped
                                                    : state_;
}

// Process address-space state and synchronization.
AddressSpace& Task::addressSpace() { return addressSpace_; }

// Process-local capability handle table.
HandleTable& Task::handles() { return handles_; }

// Borrowed parent pointer; use parentReference when continuing after unlock.
Task* Task::parent() {
    InterruptSpinLockGuard guard(lock_);
    return parent_;
}
// Parent ID read while the relationship is protected.
uint64_t Task::parentId() {
    InterruptSpinLockGuard guard(lock_);
    return parent_ ? parent_->id() : 0;
}
// Take the reference before dropping the relationship lock; reparenting must
// not leave a caller using an unretained former parent.
TaskReference Task::parentReference() {
    InterruptSpinLockGuard guard(lock_);
    return TaskReference(parent_);
}
// Detach our child list, then transfer each parent reference to a live init or
// the kernel task. Link under adopter and child locks, release the old parent
// reference afterward, and notify the adopter about possible zombies.
void Task::adoptChildren() {
    Task* adopter = initTask && initTask != this && !initTask->isTerminated()
                        ? initTask
                        : kernelTask;
    Task* children;
    {
        InterruptSpinLockGuard guard(lock_);
        children    = firstChild_;
        firstChild_ = nullptr;
    }
    if (!children) return;
    while (children) {
        Task* child = children;
        children    = child->nextSibling_;
        {
            InterruptSpinLockGuard parentGuard(adopter->lock_);
            InterruptSpinLockGuard childGuard(child->lock_);
            adopter->retain();
            child->parent_       = adopter;
            child->nextSibling_  = adopter->firstChild_;
            adopter->firstChild_ = child;
        }
        release();
    }
    if (adopter) adopter->notifyChildStateChange();
}

// Logical termination state, independent of physical resource reclamation.
bool Task::isTerminated() { return state() == TaskState::Terminated; }

// Copy process metadata before making the child runnable, then preserve
// inheritable handle numbers. NoInherit slots are skipped; endpoint/shared
// mapping helpers retain their object-specific ownership rules. Any failure
// leaves partial child resources for the reclaimer, not the caller stack.
bool Task::inheritForFork(Task& parent) {
    {
        InterruptSpinLockGuard parentGuard(parent.lock_);
        InterruptSpinLockGuard childGuard(lock_);
        capabilities_       = parent.capabilities_;
        uid_                = parent.uid_;
        gid_                = parent.gid_;
        effectiveUid_       = parent.effectiveUid_;
        effectiveGid_       = parent.effectiveGid_;
        savedUid_           = parent.savedUid_;
        savedGid_           = parent.savedGid_;
        supplementaryCount_ = parent.supplementaryCount_;
        for (uint32_t i = 0; i < supplementaryCount_; ++i)
            supplementaryGroups_[i] = parent.supplementaryGroups_[i];
        memlockLimit_=parent.memlockLimit_;memlockHardLimit_=parent.memlockHardLimit_;
        coreLimit_         = parent.coreLimit_;
        coreHardLimit_     = parent.coreHardLimit_;
        niceLimit_         = parent.niceLimit_;
        niceHardLimit_     = parent.niceHardLimit_;
        dumpable_          = parent.dumpable_;
        imageMappingCount_ = parent.imageMappingCount_;
        for (uint32_t i = 0; i < imageMappingCount_; ++i)
            imageMappings_[i] = parent.imageMappings_[i];
        for (unsigned i = 0; i < 14; ++i) auxiliary_[i] = parent.auxiliary_[i];
        bootstrapHandle_ = parent.bootstrapHandle_;
        kernelHandle_    = parent.kernelHandle_;
        for (uint32_t index = 0; index < 3; ++index) {
            standardHandles_[index] = parent.standardHandles_[index];
        }
        taskGroupId_           = parent.taskGroupId_;
        sessionId_             = parent.sessionId_;
        authenticationSession_ = parent.authenticationSession_;
        authenticationUid_     = parent.authenticationUid_;
        authenticationGid_     = parent.authenticationGid_;
        commandLineSize_       = parent.commandLineSize_;
        for (size_t i = 0; i < sizeof(commandLine_); ++i)
            commandLine_[i] = parent.commandLine_[i];
        for (uint32_t signal = 0; signal <= maximumSignal; ++signal) {
            signalActions_[signal] = parent.signalActions_[signal];
        }
        pendingSignals_ = 0;
    }

    const uint32_t inheritedCapacity = parent.handles_.capacity();
    for (uint32_t index = 0; index < inheritedCapacity; ++index) {
        uint64_t handle = 0;
        void* object    = nullptr;
        HandleType type = HandleType::None;
        uint32_t rights = 0;
        if (!parent.handles_.inspectAt(index, &handle, &object, &type,
                                       &rights)) {
            continue;
        }
        uint32_t flags = 0;
        if (!parent.handles_.getFlags(handle, flags)) return false;
        if (flags & os::handleFlagNoInherit) continue;
        uint64_t inherited = 0;
        if (type == HandleType::Endpoint) {
            inherited =
                Endpoint::duplicateHandle(&parent, this, handle, handle);
        } else if (type == HandleType::SharedMemory) {
            inherited = ::kernel::mm::sharedMemory().duplicateHandle(
                parent, *this, handle, handle);
        } else
            inherited = parent.handles_.copyTo(handles_, handle, handle);
        if (inherited != handle) {
            // The reclaimer closes endpoints before disposing handles,
            // including recoverable receivers whose identity must survive this
            // instance.
            return false;
        }
        handles_.setFlags(inherited, flags);
    }
    return true;
}

// Commit a prepared root for the calling thread only. Once sibling threads are
// detached, ownership must reach a committed state even under concurrent kill.
// Keep interrupts available for remote coordination while preventing local
// preemption throughout this irreversible section.
bool Task::commitExec(uint64_t newRootPhysicalAddress, const char* name,
                      Thread* currentThread) {
    if (newRootPhysicalAddress == 0 || currentThread == nullptr ||
        currentThread->getTask() != this || this == kernelTask) {
        return false;
    }
    // Once sibling threads are detached, their ownership and the new root must
    // reach a committed state even if another CPU kills this task. Interrupts
    // remain enabled for shootdowns, but no blocking/scheduling is allowed
    // here.
    cpu::PreemptionGuard commitGuard;
    uint64_t oldRoot       = 0;
    Thread* retiredThreads = nullptr;
    {
        InterruptSpinLockGuard guard(lock_);
        if (!acceptsThreads_ || state_ == TaskState::Terminated ||
            currentThread->getState() == ThreadState::Terminated)
            return false;
        for (Thread* thread = threads_; thread != nullptr;) {
            Thread* next = thread->getTaskNext();
            if (thread != currentThread &&
                thread->getState() != ThreadState::Terminated) {
                task::terminateThread(thread);
                cpu::requestRemoteReschedule(
                    static_cast<uint32_t>(thread->getCpuId()));
            }
            if (thread != currentThread) {
                thread->setExitAccounted(true);
                thread->setTaskNext(retiredThreads);
                retiredThreads = thread;
            }
            thread = next;
        }
        currentThread->setTaskNext(nullptr);
        currentThread->setPendingSignals(0);
        currentThread->signalStack       = {};
        currentThread->signalDepth       = 0;
        currentThread->activeSignalFrame = 0;
        currentThread->lastFault         = {};
        imageMappingCount_               = 0;
        for (auto& value : auxiliary_) value = 0;
        currentThread->setWaitNext(nullptr);
        threads_    = currentThread;
        mainThread_ = currentThread;
        currentThread->setId(id_);
        savedUid_         = effectiveUid_;
        savedGid_         = effectiveGid_;
        dumpable_         = uid_ == effectiveUid_ && gid_ == effectiveGid_;
        threadCount_      = 1;
        acceptsThreads_   = false;
        state_            = TaskState::Running;
        stopped_          = false;
        stopSignal_       = 0;
        stopReported_     = false;
        continuedPending_ = false;
        pendingSignals_   = 0;
        dumpable_         = uid_ == effectiveUid_ && gid_ == effectiveGid_;
        serviceRole_      = os::ServiceRole::None;
        for (uint32_t signal = 1; signal <= maximumSignal; ++signal) {
            if (signalActions_[signal].getHandler() != signalIgnore) {
                signalActions_[signal] = {};
            }
        }
        copyName(name_, name);
    }
    // Sibling states were changed under the task lock, but remote CPUs may
    // still execute them. Drop that lock and wait for completed switches
    // before cancelling stack-owned waits, freeing stacks, or destroying the
    // old root.
    task::quiesceTask(this);
    device::resourceDomains().ownerExiting(*this);
    for (Thread* thread = retiredThreads; thread;
         thread         = thread->getTaskNext()) {
        Endpoint::cancelThread(thread);
        thread->cancelWait();
        ThreadCleanup::releaseAll(*thread);
    }
    while (retiredThreads) {
        Thread* thread = retiredThreads;
        retiredThreads = thread->getTaskNext();
        if (thread->getKernelStack()) mm::freeHeap(thread->getKernelStack());
        mm::freeHeap(thread);
    }
    // Release old mapping registrations before swapping roots; they describe
    // the old generation. Handle close-on-exec then runs against the committed
    // image, while the detached old root remains locally owned for
    // destruction.
    mm::maps().releaseTask(*this);
    ::kernel::mm::sharedMemory().releaseTaskMappings(*this);
    {
        PreemptionSpinLockGuard guard(addressSpace_.lock());
        oldRoot = addressSpace_.replaceRoot(newRootPhysicalAddress);
        mm::switchAddressSpace(newRootPhysicalAddress);
    }
    handles_.closeOnExec();
    mm::destroyAddressSpaceRoot(oldRoot);
    device::resourceDomains().addressSpaceReleased(*this);
    {
        InterruptSpinLockGuard guard(lock_);
        acceptsThreads_ = state_ != TaskState::Terminated;
    }
    return true;
}

// Process-group identity.
uint64_t Task::taskGroupId() const { return taskGroupId_; }

// Session identity.
uint64_t Task::sessionId() const { return sessionId_; }
// Store bounded NUL-separated arguments for task information/core notes. The
// internal console-selection argument is omitted from the public list.
void Task::setCommandLine(uint32_t count, const char* const* arguments) {
    size_t used = 0;
    for (uint32_t i = 0; i < count && used + 1 < sizeof(commandLine_); ++i) {
        const char* text = arguments ? arguments[i] : nullptr;
        if (!text) continue;
        constexpr char prefix[] = "--os-terminal=";
        size_t p                = 0;
        while (prefix[p] && prefix[p] == text[p]) ++p;
        if (!prefix[p] && text[p] >= '1' && text[p] <= '7' && !text[p + 1]) {
            continue;
        }
        while (*text && used + 1 < sizeof(commandLine_))
            commandLine_[used++] = *text++;
        commandLine_[used++] = 0;
    }
    commandLineSize_ = used;
}

// Resolve the requested leader with a retained reference and require the same
// session. A zero group requests our own PID; no cross-session joining is
// permitted by this mechanism.
bool Task::setTaskGroup(uint64_t taskGroupId) {
    if (taskGroupId == 0) taskGroupId = id_;
    auto leader = task::find(taskGroupId);
    if (leader == nullptr || leader->sessionId() != sessionId_) return false;
    InterruptSpinLockGuard guard(lock_);
    if (leader->sessionId() != sessionId_) return false;
    taskGroupId_ = taskGroupId;
    return true;
}

// Reject an existing group leader; otherwise create SID/PGID from this PID and
// detach the terminal. An existing authentication record follows the new
// session identity instead of keeping the previous session number.
bool Task::createSession() {
    InterruptSpinLockGuard guard(lock_);
    if (taskGroupId_ == id_) {
        return false;
    }
    sessionId_ = id_;
    if (authenticationSession_) authenticationSession_ = id_;
    taskGroupId_  = id_;
    terminalKind_ = terminalId_ = 0;
    return true;
}

// Attach the bootstrap thread to the kernel task, then create reclamation,
// core timeout, device-domain, and service-recovery infrastructure before user
// processes can depend on asynchronous cleanup.
void init() {
    taskListLock.init();
    taskList      = nullptr;
    void* storage = mm::allocateHeap(sizeof(Task));
    Task* task    = storage == nullptr ? nullptr : new (storage) Task;
    if (task == nullptr ||
        !task->init("kernel", mm::getKernelAddressSpaceRoot(), nullptr)) {
        device::serialWrite("[TASK] unable to init kernel task\n");
        cpu::halt();
    }
    kernelTask = task;
    taskList   = task;
    kernelTask->attachThread(currentThread());
    cpu::getCurrent()->setCurrentTask(kernelTask);
    taskReclaimer().init();
    coreDump().init();
    device::resourceDomains().init();
    serviceRecovery().init();
    device::serialWrite("[TASK] kernel task initialized\n");
}

// Current CPU task, which can be null for ownerless kernel workers.
Task* getCurrent() { return cpu::getCurrent()->getCurrentTask(); }

// Kernel task object; per-CPU idle threads are stored in their run queues.
Task* getIdle() { return kernelTask; }

// Retain under the global-list lock so removal cannot invalidate the returned
// object immediately after the lookup releases that lock.
TaskReference find(uint64_t taskId) {
    InterruptSpinLockGuard guard(taskListLock);
    for (Task* task = taskList; task != nullptr; task = task->nextGlobal_) {
        if (task->id() == taskId) return task;
    }
    return nullptr;
}

// Index-based enumeration skips the kernel task. List positions are not stable
// across separate calls when other tasks are created or removed.
TaskReference at(uint32_t index) {
    InterruptSpinLockGuard guard(taskListLock);
    for (Task* task = taskList; task; task = task->nextGlobal_) {
        if (task == kernelTask) continue;
        if (!index--) return task;
    }
    return nullptr;
}

// Return the lowest non-kernel PID at or above minimumId. Advancing by PID
// avoids relying on unstable list positions during multi-call enumeration.
TaskReference next(uint64_t minimumId) {
    InterruptSpinLockGuard guard(taskListLock);
    Task* selected = nullptr;
    for (Task* task = taskList; task; task = task->nextGlobal_)
        if (task != kernelTask && task->id() >= minimumId &&
            (!selected || task->id() < selected->id()))
            selected = task;
    return selected;
}

// Read the foreground process group for a console or PTY terminal slot.
uint64_t terminalForeground(uint32_t kind, uint32_t index) {
    return kind >= 1 && kind <= 2 && index < 8
               ? __atomic_load_n(&terminalForegroundGroups[kind - 1][index],
                                 __ATOMIC_ACQUIRE)
               : 0;
}
// Publish a terminal's foreground group; caller-role authorization belongs to
// the task service, while this helper checks kind and slot bounds.
bool setTerminalForeground(uint32_t kind, uint32_t index, uint64_t group) {
    if (kind < 1 || kind > 2 || index >= (kind == 1 ? 6U : 8U)) return false;
    __atomic_store_n(&terminalForegroundGroups[kind - 1][index], group,
                     __ATOMIC_RELEASE);
    return true;
}

// Take bounded batches of retained tasks, then deliver outside the global list
// lock. Delivery may block or terminate the sender, so neither a raw list
// pointer nor that lock may survive into the delivery operation.
bool sendSignalToTaskGroup(uint64_t taskGroupId, uint32_t signal,
                           Task* authenticatedCaller) {
    if (taskGroupId == 0 || (signal != 0 && !task::isValidSignal(signal)))
        return false;
    bool delivered  = false;
    uint64_t before = UINT64_MAX;
    while (true) {
        TaskReference batch[32];
        uint32_t count = 0;
        {
            InterruptSpinLockGuard guard(taskListLock);
            for (Task* task = taskList; task && count < 32;
                 task       = task->nextGlobal_) {
                if (task->id() >= before) continue;
                batch[count++] = TaskReference(task);
            }
        }
        if (!count) break;
        before = batch[count - 1]->id();
        // Delivery can block or terminate the caller. Never retain the global
        // task-list lock across it; task objects outlive their address spaces.
        for (uint32_t i = 0; i < count; ++i) {
            Task* task = batch[i];
            Task* caller =
                authenticatedCaller ? authenticatedCaller : task::getCurrent();
            const bool permitted = !caller || caller->effectiveUid() == 0 ||
                                   caller->uid() == task->uid() ||
                                   caller->effectiveUid() == task->uid();
            if (permitted && (!signal || !task->managedService()) &&
                task->taskGroupId() == taskGroupId)
                delivered = (signal == 0 ? !task->isTerminated()
                                         : task->deliverSignal(signal)) ||
                            delivered;
        }
    }
    return delivered;
}

// Remove only list membership under the global lock. The caller separately
// transfers or releases the reference that kept the list-owned object alive.
void Task::unlinkGlobal() {
    InterruptSpinLockGuard guard(taskListLock);
    Task** link = &taskList;
    while (*link && *link != this) link = &(*link)->nextGlobal_;
    if (*link == this) *link = nextGlobal_;
    nextGlobal_ = nullptr;
}

}  // namespace kernel::task
