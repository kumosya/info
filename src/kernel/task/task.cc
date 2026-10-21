#include "kernel/task.h"
#include "kernel/recovery.h"
#include "kernel/time.h"

#include "kernel/atomic.h"
#include "kernel/device.h"
#include "kernel/cpu.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/new.h"
#include "kernel/serial.h"
#include "kernel/signal.h"
#include "kernel/smp.h"

namespace {

constexpr uint32_t initialHandleCapacity = 64;
kernel::Atomic<uint64_t> nextTaskId{1};
kernel::Task* kernelTask;
kernel::Task* initTask;
kernel::Task* taskList;
kernel::SpinLock taskListLock;
uint64_t terminalForegroundGroups[2][8]{};

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

// Resource reclamation must survive the death of the requesting user thread.
// The queue owns a reference until the dedicated kernel worker has finished.
class TaskReclaimer {
   public:
    static void init() {
        worker_ = Thread::create(nullptr, run, nullptr);
        if (!worker_) {
            serial::serialWrite("[TASK] unable to initialize task reclaimer\n");
            cpu::halt();
        }
    }
    static void submit(Task& task) {
        InterruptSpinLockGuard guard(lock_);
        if (__atomic_load_n(&task.resourceState_, __ATOMIC_ACQUIRE) != 0) return;
        task.retain();
        __atomic_store_n(&task.resourceState_, 1U, __ATOMIC_RELEASE);
        task.reclaimNext_ = nullptr;
        if (tail_) tail_->reclaimNext_ = &task;
        else head_ = &task;
        tail_ = &task;
        Thread::wake(worker_);
    }
   private:
    static void run(void*) {
        for (;;) {
            Task* task;
            {
                InterruptSpinLockGuard guard(lock_);
                task = head_;
                if (task) {
                    head_ = task->reclaimNext_;
                    if (!head_) tail_ = nullptr;
                    task->reclaimNext_ = nullptr;
                } else Thread::prepareWait();
            }
            if (!task) {
                Thread::blockCurrent();
                continue;
            }
            const bool exited = task->destroyResources();
            __atomic_store_n(&task->resourceState_, 2U, __ATOMIC_RELEASE);
            // The requester may have died before notifying the parent.
            if (exited) {
                auto parent = task->parentReference();
                if (parent) {
                    parent->notifyChildStateChange();
                    parent->deliverSignal(static_cast<uint32_t>(Signal::Child));
                }
            }
            task->release();
        }
    }
    inline static SpinLock lock_;
    inline static Thread* worker_ = nullptr;
    inline static Task* head_ = nullptr;
    inline static Task* tail_ = nullptr;
};

TaskReference::TaskReference(Task* task) : task_(task) {
    const uint64_t flags = interrupt::disable();
    if (task_) {
        task_->retain();
        cleanup_.attach(task_, [](void* value) { static_cast<Task*>(value)->release(); });
    }
    interrupt::restore(flags);
}
TaskReference::TaskReference(TaskReference&& other) : task_(other.task_) {
    const uint64_t flags = interrupt::disable();
    other.cleanup_.detach();
    if (task_) cleanup_.attach(task_, [](void* value) { static_cast<Task*>(value)->release(); });
    other.task_ = nullptr;
    interrupt::restore(flags);
}
TaskReference& TaskReference::operator=(TaskReference&& other) {
    if (this != &other) {
        const uint64_t flags = interrupt::disable();
        cleanup_.reset();
        task_ = other.task_;
        other.cleanup_.detach();
        if (task_) cleanup_.attach(task_, [](void* value) { static_cast<Task*>(value)->release(); });
        other.task_ = nullptr;
        interrupt::restore(flags);
    }
    return *this;
}
TaskReference::~TaskReference() = default;
void Task::retain() { __atomic_add_fetch(&references_, 1U, __ATOMIC_RELAXED); }
void Task::release() {
    if (__atomic_sub_fetch(&references_, 1U, __ATOMIC_ACQ_REL) != 0) return;
    Task* parent = parent_;
    heap::kernelFree(this);
    if (parent) parent->release();
}
void Task::setInitTask(Task* task) {
    if (task) task->retain();
    initTask = task;
}

void AddressSpace::init(uint64_t rootPhysicalAddress) {
    rootPhysicalAddress_ = rootPhysicalAddress;
    generation_ = 1;
    lock_.init();
}

uint64_t AddressSpace::replaceRoot(uint64_t rootPhysicalAddress) {
    const uint64_t previous = rootPhysicalAddress_;
    rootPhysicalAddress_    = rootPhysicalAddress;
    if (previous != rootPhysicalAddress && ++generation_ == 0) ++generation_;
    return previous;
}

uint64_t AddressSpace::rootPhysicalAddress() const {
    return rootPhysicalAddress_;
}

SpinLock& AddressSpace::lock() { return lock_; }

bool HandleTable::init(uint32_t capacity, Task* owner, bool escrow) {
    owner_ = owner;
    escrow_ = escrow;
    disposed_ = false;
    capacity_ = 0;
    lock_.init();
    entries_ = static_cast<HandleEntry*>(
        heap::kernelAllocate(sizeof(HandleEntry) * capacity));
    if (entries_ == nullptr) {
        return false;
    }
    clearMemory(entries_, sizeof(HandleEntry) * capacity);
    capacity_ = capacity;
    lock_.init();
    return true;
}

uint32_t HandleTable::capacity() {
    InterruptSpinLockGuard guard(lock_);
    return capacity_;
}

bool HandleTable::growLocked(uint32_t required) {
    if (disposed_) return false;
    uint32_t capacity = capacity_;
    while (capacity < required) {
        if (capacity == 0 || capacity > UINT32_MAX / 2) return false;
        capacity *= 2;
    }
    if (capacity == capacity_) return true;
    auto* entries = static_cast<HandleEntry*>(heap::kernelAllocate(sizeof(HandleEntry) * capacity));
    if (!entries) return false;
    clearMemory(entries, sizeof(HandleEntry) * capacity);
    for (uint32_t i = 0; i < capacity_; ++i) entries[i] = entries_[i];
    heap::kernelFree(entries_);
    entries_ = entries;
    capacity_ = capacity;
    return true;
}

uint64_t HandleTable::add(void* object, HandleType type, uint32_t rights) {
    if (object == nullptr || type == HandleType::None) {
        return 0;
    }
    auto* reference = static_cast<Reference*>(heap::kernelAllocate(sizeof(Reference)));
    if (!reference) return 0;
    *reference = {object, type, 1};
    if (type == HandleType::Task) static_cast<Task*>(object)->retain();
    if (type == HandleType::Endpoint) static_cast<Endpoint*>(object)->retain();
    InterruptSpinLockGuard guard(lock_);
    const uint64_t result = installLocked(reference, rights, 0);
    if (!result) {
        if (type == HandleType::Task) static_cast<Task*>(object)->release();
        if (type == HandleType::Endpoint) static_cast<Endpoint*>(object)->release();
        heap::kernelFree(reference);
    }
    return result;
}

uint64_t HandleTable::installLocked(Reference* reference, uint32_t rights,
                                   uint32_t flags) {
    if (disposed_) return 0;
    for (uint32_t index = 0;; ++index) {
        if (index == capacity_) {
            if (!growLocked(capacity_ + 1)) return 0;
        }
        HandleEntry& entry = entries_[index];
        if (entry.getObject() != nullptr) {
            continue;
        }
        entry.setGeneration(entry.getGeneration() + 1);
        if (entry.getGeneration() == 0) {
            entry.setGeneration(entry.getGeneration() + 1);
        }
        entry.reference = reference;
        entry.flags = flags;
        entry.setObject(reference->object);
        entry.setRights(rights);
        entry.setType(static_cast<uint16_t>(reference->type));
        return (static_cast<uint64_t>(entry.getGeneration()) << 32) |
               (index + 1);
    }
    return 0;
}

bool HandleTable::addAt(uint64_t handle, void* object, HandleType type,
                        uint32_t rights) {
    const uint32_t encodedIndex = static_cast<uint32_t>(handle);
    const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
    if (object == nullptr || type == HandleType::None || encodedIndex == 0 ||
        generation == 0) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    if (disposed_ || !growLocked(encodedIndex)) return false;
    HandleEntry& entry = entries_[encodedIndex - 1];
    if (entry.getObject() != nullptr) return false;
    auto* reference = static_cast<Reference*>(heap::kernelAllocate(sizeof(Reference)));
    if (!reference) return false;
    *reference = {object, type, 1};
    if (type == HandleType::Task) static_cast<Task*>(object)->retain();
    if (type == HandleType::Endpoint) static_cast<Endpoint*>(object)->retain();
    entry.reference = reference;
    entry.flags = 0;
    entry.setObject(object);
    entry.setRights(rights);
    entry.setType(static_cast<uint16_t>(type));
    entry.setGeneration(generation);
    return true;
}

void* HandleTable::get(uint64_t handle, HandleType type,
                       uint32_t requiredRights) {
    const uint32_t encodedIndex = static_cast<uint32_t>(handle);
    const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
    InterruptSpinLockGuard guard(lock_);
    if (encodedIndex == 0 || encodedIndex > capacity_ || generation == 0) {
        return nullptr;
    }
    const HandleEntry& entry = entries_[encodedIndex - 1];
    if (entry.getObject() == nullptr || entry.getGeneration() != generation ||
        entry.getType() != static_cast<uint16_t>(type) ||
        (entry.getRights() & requiredRights) != requiredRights) {
        return nullptr;
    }
    return entry.getObject();
}

bool HandleTable::inspect(uint64_t handle, void** object, HandleType* type,
                          uint32_t* rights) {
    const uint32_t encodedIndex = static_cast<uint32_t>(handle);
    const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
    InterruptSpinLockGuard guard(lock_);
    if (object == nullptr || type == nullptr || rights == nullptr ||
        encodedIndex == 0 || encodedIndex > capacity_ || generation == 0) {
        return false;
    }
    const HandleEntry& entry = entries_[encodedIndex - 1];
    if (entry.getObject() == nullptr || entry.getGeneration() != generation) {
        return false;
    }
    *object = entry.getObject();
    *type   = static_cast<HandleType>(entry.getType());
    *rights = entry.getRights();
    return true;
}

bool HandleTable::inspectAt(uint32_t index, uint64_t* handle, void** object,
                            HandleType* type, uint32_t* rights) {
    InterruptSpinLockGuard guard(lock_);
    if (handle == nullptr || object == nullptr || type == nullptr ||
        rights == nullptr || index >= capacity_) {
        return false;
    }
    const HandleEntry& entry = entries_[index];
    if (entry.getObject() == nullptr || entry.getGeneration() == 0)
        return false;
    *handle =
        (static_cast<uint64_t>(entry.getGeneration()) << 32) | (index + 1);
    *object = entry.getObject();
    *type   = static_cast<HandleType>(entry.getType());
    *rights = entry.getRights();
    return true;
}

bool HandleTable::close(uint64_t handle) {
    const uint32_t encodedIndex = static_cast<uint32_t>(handle);
    const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
    Reference* reference;
    {
        InterruptSpinLockGuard guard(lock_);
        if (encodedIndex == 0 || encodedIndex > capacity_ || generation == 0)
            return false;
        HandleEntry& entry = entries_[encodedIndex - 1];
        if (entry.getObject() == nullptr ||
            entry.getGeneration() != generation) {
            return false;
        }
        reference = entry.reference;
        if (escrow_) __atomic_sub_fetch(&reference->escrow, 1U, __ATOMIC_ACQ_REL);
        entry.reference = nullptr;
        entry.flags = 0;
        entry.setObject(nullptr);
        entry.setRights(0);
        entry.setType(static_cast<uint16_t>(HandleType::None));
    }
    if (reference->type == HandleType::Endpoint && owner_ &&
        !hasObjectRights(reference->object, HandleType::Endpoint, handleRightRead))
        static_cast<Endpoint*>(reference->object)->receiverClosed(owner_);
    Reference::release(reference);
    return true;
}

void HandleTable::Reference::retain(Reference* reference) {
    __atomic_add_fetch(&reference->count, 1U, __ATOMIC_RELAXED);
}

HandleTable::Pin::Pin(HandleTable& table, uint64_t handle, HandleType type, uint32_t rights) {
    InterruptSpinLockGuard guard(table.lock_);
    if (!table.validLocked(handle)) return;
    const auto& entry = table.entries_[static_cast<uint32_t>(handle) - 1];
    if (entry.reference->type != type || (entry.getRights() & rights) != rights) return;
    reference_ = entry.reference;
    Reference::retain(reference_);
    cleanup_.attach(reference_, [](void* value) { Reference::release(static_cast<Reference*>(value)); });
}
HandleTable::Pin::~Pin() = default;

void HandleTable::Reference::release(Reference* reference) {
    if (__atomic_sub_fetch(&reference->count, 1U, __ATOMIC_ACQ_REL) != 0) return;
    void* object = reference->object;
    const HandleType type = reference->type;
    if (type == HandleType::SharedMemory) {
        SharedMemory::closeHandleObject(object);
    } else if (type == HandleType::PageObject) {
        static_cast<memory::PageObject*>(object)->release();
    } else if (type == HandleType::Task) {
        static_cast<Task*>(object)->release();
    } else if (type == HandleType::Endpoint) {
        static_cast<Endpoint*>(object)->release();
    } else if (type == HandleType::InterruptBinding) {
        static_cast<device::InterruptBinding*>(object)->destroy();
    } else if (type == HandleType::DmaBuffer) {
        static_cast<device::DmaBuffer*>(object)->destroy();
    } else if (type == HandleType::MmioRegion) {
        static_cast<device::MmioRegion*>(object)->destroy();
    } else if (type == HandleType::DeviceDomain) {
        static_cast<device::ResourceDomain*>(object)->closeLease();
    }
    heap::kernelFree(reference);
}

bool HandleTable::validLocked(uint64_t handle) const {
    const uint32_t slot = static_cast<uint32_t>(handle);
    return slot && slot <= capacity_ && (handle >> 48) == 0 &&
           entries_[slot - 1].getObject() &&
           entries_[slot - 1].getGeneration() == (handle >> 32);
}

uint64_t HandleTable::duplicate(uint64_t handle, uint32_t rights, uint32_t flags) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle) || (flags & ~os::handleFlagsMask)) return 0;
    HandleEntry& entry = entries_[static_cast<uint32_t>(handle) - 1];
    if ((rights & entry.getRights()) != rights) return 0;
    Reference* reference = entry.reference;
    Reference::retain(reference);
    const uint64_t result = installLocked(reference, rights, flags);
    if (!result) Reference::release(reference);
    return result;
}

bool HandleTable::restrictRights(uint64_t handle, uint32_t rights) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle)) return false;
    HandleEntry& entry = entries_[static_cast<uint32_t>(handle) - 1];
    if ((rights & entry.getRights()) != rights) return false;
    entry.setRights(rights);
    return true;
}

uint64_t HandleTable::copyTo(HandleTable& destination, uint64_t handle,
                             uint64_t destinationHandle) {
    if (&destination == this) {
        void* object;
        HandleType type;
        uint32_t rights;
        return inspect(handle, &object, &type, &rights) ? duplicate(handle, rights) : 0;
    }
    HandleTable* first = reinterpret_cast<uintptr_t>(this) < reinterpret_cast<uintptr_t>(&destination)
        ? this : &destination;
    HandleTable* second = first == this ? &destination : this;
    InterruptSpinLockGuard firstGuard(first->lock_);
    InterruptSpinLockGuard secondGuard(second->lock_);
    if (destination.disposed_ || !validLocked(handle)) return 0;
    auto& source = entries_[static_cast<uint32_t>(handle) - 1];
    if (destinationHandle) {
        const uint32_t slot = static_cast<uint32_t>(destinationHandle);
        const uint64_t generation = destinationHandle >> 32;
        if (!slot || !generation || generation > UINT16_MAX ||
            !destination.growLocked(slot) || destination.entries_[slot - 1].getObject()) return 0;
        auto& entry = destination.entries_[slot - 1];
        entry = source;
        entry.setGeneration(static_cast<uint16_t>(generation));
        Reference::retain(entry.reference);
        if (destination.escrow_) __atomic_add_fetch(&entry.reference->escrow, 1U, __ATOMIC_RELAXED);
        return destinationHandle;
    }
    Reference::retain(source.reference);
    const uint64_t result = destination.installLocked(source.reference, source.getRights(), source.flags);
    if (result && destination.escrow_) __atomic_add_fetch(&source.reference->escrow, 1U, __ATOMIC_RELAXED);
    if (!result) Reference::release(source.reference);
    return result;
}

bool HandleTable::setFlags(uint64_t handle, uint32_t flags) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle) || (flags & ~os::handleFlagsMask)) return false;
    entries_[static_cast<uint32_t>(handle) - 1].flags = flags;
    return true;
}

bool HandleTable::getFlags(uint64_t handle, uint32_t& flags) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle)) return false;
    flags = entries_[static_cast<uint32_t>(handle) - 1].flags;
    return true;
}

bool HandleTable::hasObjectRights(void* object, HandleType type, uint32_t rights) {
    InterruptSpinLockGuard guard(lock_);
    for (uint32_t i = 0; i < capacity_; ++i) {
        const auto& entry = entries_[i];
        if (entry.getObject() == object && entry.getType() == static_cast<uint16_t>(type) &&
            (entry.getRights() & rights) == rights) return true;
    }
    return false;
}
int64_t HandleTable::references(uint64_t handle) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle)) return -2;
    const auto* reference = entries_[static_cast<uint32_t>(handle) - 1].reference;
    return static_cast<int64_t>(__atomic_load_n(&reference->count, __ATOMIC_ACQUIRE)) -
        __atomic_load_n(&reference->escrow, __ATOMIC_ACQUIRE);
}

bool HandleTable::Transfer::capture(HandleTable& source, const os::IpcMessage& message,
                                    bool commitMove) {
    if (count_ || message.handleCount > 4) return false;
    InterruptSpinLockGuard guard(source.lock_);
    for (uint32_t i = 0; i < message.handleCount; ++i) {
        if (!source.validLocked(message.handles[i])) return false;
        const auto& entry = source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
        const auto type = entry.reference->type;
        if (type == HandleType::MmioRegion || type == HandleType::DmaBuffer ||
            type == HandleType::InterruptBinding || type == HandleType::DeviceDomain) return false;
        if (message.handleOptions[i] & ~(os::ipcHandleRestrictRights | os::ipcHandleMove)) return false;
        if ((message.handleOptions[i] & os::ipcHandleRestrictRights) &&
            (message.handleRights[i] & entry.getRights()) != message.handleRights[i]) return false;
        for (uint32_t j = 0; j < i; ++j)
            if (message.handles[i] == message.handles[j] &&
                ((message.handleOptions[i] | message.handleOptions[j]) & os::ipcHandleMove)) return false;
    }
    for (uint32_t i = 0; i < message.handleCount; ++i) {
        auto& entry = source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
        references_[i] = entry.reference;
        rights_[i] = (message.handleOptions[i] & os::ipcHandleRestrictRights)
            ? message.handleRights[i] : entry.getRights();
        Reference::retain(references_[i]);
    }
    count_ = message.handleCount;
    // Capture is the enqueue commit point. No further fallible step may follow
    // it in send; move leaves the queued reference as the new owner.
    for (uint32_t i = 0; i < count_; ++i) {
        if (!(message.handleOptions[i] & os::ipcHandleMove)) continue;
        if (!commitMove) {
            moveSource_ = &source;
            moveHandles_[i] = message.handles[i];
            continue;
        }
        auto& entry = source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
        entry.reference = nullptr;
        entry.setObject(nullptr);
        entry.setRights(0);
        entry.setType(static_cast<uint16_t>(HandleType::None));
        entry.flags = 0;
        Reference::release(references_[i]);
    }
    return true;
}

bool HandleTable::Transfer::install(HandleTable& destination, os::IpcMessage& message) {
    if (!moveSource_ || moveSource_ == &destination) {
        InterruptSpinLockGuard guard(destination.lock_);
        return installLocked(destination, message);
    }
    HandleTable* first = reinterpret_cast<uintptr_t>(moveSource_) < reinterpret_cast<uintptr_t>(&destination)
        ? moveSource_ : &destination;
    HandleTable* second = first == moveSource_ ? &destination : moveSource_;
    InterruptSpinLockGuard firstGuard(first->lock_);
    InterruptSpinLockGuard secondGuard(second->lock_);
    return installLocked(destination, message);
}

bool HandleTable::Transfer::installLocked(HandleTable& destination, os::IpcMessage& message) {
    if (destination.disposed_) return false;
    if (moveSource_) {
        for (uint32_t i = 0; i < count_; ++i)
            if (moveHandles_[i] && (!moveSource_->validLocked(moveHandles_[i]) ||
                moveSource_->entries_[static_cast<uint32_t>(moveHandles_[i]) - 1].reference != references_[i]))
                return false;
    }
    uint32_t free = 0;
    for (uint32_t i = 0; i < destination.capacity_; ++i)
        if (!destination.entries_[i].getObject()) ++free;
    if (free < count_ && !destination.growLocked(destination.capacity_ + count_ - free))
        return false;
    if (moveSource_) {
        for (uint32_t i = 0; i < count_; ++i) {
            if (!moveHandles_[i]) continue;
            auto& entry = moveSource_->entries_[static_cast<uint32_t>(moveHandles_[i]) - 1];
            entry.reference = nullptr;
            entry.setObject(nullptr);
            entry.setRights(0);
            entry.setType(static_cast<uint16_t>(HandleType::None));
            entry.flags = 0;
            Reference::release(references_[i]);
            moveHandles_[i] = 0;
        }
        moveSource_ = nullptr;
    }
    for (uint32_t i = 0; i < count_; ++i) {
        message.handles[i] = destination.installLocked(references_[i], rights_[i], 0);
        message.handleRights[i] = rights_[i];
        message.handleOptions[i] = 0;
        references_[i] = nullptr;
    }
    message.handleCount = count_;
    count_ = 0;
    return true;
}

void HandleTable::Transfer::clear() {
    for (uint32_t i = 0; i < count_; ++i) {
        Reference::release(references_[i]);
        references_[i] = nullptr;
    }
    count_ = 0;
    moveSource_ = nullptr;
    for (auto& handle : moveHandles_) handle = 0;
}

void HandleTable::closeOnExec() {
    for (uint32_t i = 0; i < capacity(); ++i) {
        uint64_t handle = 0;
        {
            InterruptSpinLockGuard guard(lock_);
            const auto& entry = entries_[i];
            if (entry.getObject() && (entry.flags & os::handleFlagCloseOnExec))
                handle = (static_cast<uint64_t>(entry.getGeneration()) << 32) | (i + 1);
        }
        if (handle) close(handle);
    }
}

void HandleTable::dispose() {
    { InterruptSpinLockGuard guard(lock_); disposed_ = true; }
    closeAll();
    InterruptSpinLockGuard guard(lock_);
    heap::kernelFree(entries_);
    entries_ = nullptr;
    capacity_ = 0;
}

void HandleTable::closeAll() {
    for (uint32_t index = 0; index < capacity_; ++index) {
        uint64_t handle = 0;
        {
            InterruptSpinLockGuard guard(lock_);
            const HandleEntry& entry = entries_[index];
            if (entry.getObject() != nullptr) {
                handle = (static_cast<uint64_t>(entry.getGeneration()) << 32) |
                         (index + 1);
            }
        }
        if (handle != 0) {
            close(handle);
        }
    }
}

bool Task::init(const char* name, uint64_t rootPhysicalAddress, Task* parent) {
    id_ = nextTaskId.fetchAdd(1, MemoryOrder::Relaxed);
    copyName(name_, name);
    state_ = TaskState::Created;
    addressSpace_.init(rootPhysicalAddress);
    if (!handles_.init(initialHandleCapacity, this)) {
        return false;
    }
    mainThread_      = nullptr;
    threads_         = nullptr;
    threadCount_     = 0;
    parent_          = parent;
    if (parent_) parent_->retain();
    terminalId_ = parent ? parent->terminalId() : 0;
    terminalKind_ = parent ? parent->terminalKind() : 0;
    startedNanoseconds_ = time::getMonotonicTime();
    uid_ = effectiveUid_ = parent ? parent->effectiveUid() : 0;
    gid_ = effectiveGid_ = parent ? parent->effectiveGid() : 0;
    if (parent) parent->authentication(authenticationSession_, authenticationUid_, authenticationGid_);
    firstChild_      = nullptr;
    nextSibling_     = nullptr;
    childWaiters_    = nullptr;
    exitCode_        = 0;
    acceptsThreads_  = true;
    capabilities_    = 0;
    bootstrapHandle_ = 0;
    clearMemory(standardHandles_, sizeof(standardHandles_));
    taskGroupId_      = parent == nullptr ? id_ : parent->taskGroupId();
    sessionId_        = parent == nullptr ? id_ : parent->sessionId();
    stopped_          = false;
    stopSignal_       = 0;
    stopReported_     = false;
    continuedPending_ = false;
    clearMemory(signalActions_, sizeof(signalActions_));
    pendingSignals_ = 0;
    nextGlobal_     = nullptr;
    lock_.init();
    return true;
}

Task* Task::create(const char* name, Task* parent) {
    const uint64_t root = memory::createAddressSpaceRoot();
    if (root == 0) {
        return nullptr;
    }
    void* storage = heap::kernelAllocate(sizeof(Task));
    Task* task    = storage == nullptr ? nullptr : new (storage) Task;
    if (task == nullptr || !task->init(name, root, parent)) {
        memory::destroyAddressSpaceRoot(root);
        heap::kernelFree(storage);
        return nullptr;
    }
    memory::assignPhysicalPageOwner(root, task->id());
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

uint64_t Task::id() const { return id_; }

const char* Task::name() const { return name_; }

TaskState Task::state() {
    InterruptSpinLockGuard guard(lock_);
    return stopped_ && state_ == TaskState::Running ? TaskState::Stopped
                                                    : state_;
}

AddressSpace& Task::addressSpace() { return addressSpace_; }

HandleTable& Task::handles() { return handles_; }

Task* Task::parent() {
    InterruptSpinLockGuard guard(lock_);
    return parent_;
}
uint64_t Task::parentId() {
    InterruptSpinLockGuard guard(lock_);
    return parent_ ? parent_->id() : 0;
}
TaskReference Task::parentReference() {
    InterruptSpinLockGuard guard(lock_);
    return TaskReference(parent_);
}
void Task::adoptChildren() {
    Task* adopter = initTask && initTask != this && !initTask->isTerminated() ? initTask : kernelTask;
    Task* children;
    {
        InterruptSpinLockGuard guard(lock_);
        children = firstChild_;
        firstChild_ = nullptr;
    }
    if (!children) return;
    while (children) {
        Task* child = children;
        children = child->nextSibling_;
        {
            InterruptSpinLockGuard parentGuard(adopter->lock_);
            InterruptSpinLockGuard childGuard(child->lock_);
            adopter->retain();
            child->parent_ = adopter;
            child->nextSibling_ = adopter->firstChild_;
            adopter->firstChild_ = child;
        }
        release();
    }
    if (adopter) adopter->notifyChildStateChange();
}

bool Task::isTerminated() { return state() == TaskState::Terminated; }

int Task::exitCode() {
    InterruptSpinLockGuard guard(lock_);
    return exitCode_;
}

bool Task::inheritForFork(Task& parent) {
    {
        InterruptSpinLockGuard parentGuard(parent.lock_);
        InterruptSpinLockGuard childGuard(lock_);
        capabilities_    = parent.capabilities_;
        uid_ = parent.uid_;
        gid_ = parent.gid_;
        effectiveUid_ = parent.effectiveUid_;
        effectiveGid_ = parent.effectiveGid_;
        bootstrapHandle_ = parent.bootstrapHandle_;
        for (uint32_t index = 0; index < 3; ++index) {
            standardHandles_[index] = parent.standardHandles_[index];
        }
        taskGroupId_ = parent.taskGroupId_;
        sessionId_   = parent.sessionId_;
          authenticationSession_ = parent.authenticationSession_;
          authenticationUid_ = parent.authenticationUid_; authenticationGid_ = parent.authenticationGid_;
        commandLineSize_ = parent.commandLineSize_;
        for (size_t i = 0; i < sizeof(commandLine_); ++i) commandLine_[i] = parent.commandLine_[i];
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
            inherited =
                SharedMemory::duplicateHandle(parent, *this, handle, handle);
        } else inherited = parent.handles_.copyTo(handles_, handle, handle);
        if (inherited != handle) {
            // The reclaimer closes endpoints before disposing handles, including
            // recoverable receivers whose identity must survive this instance.
            return false;
        }
        handles_.setFlags(inherited, flags);
    }
    return true;
}

bool Task::commitExec(uint64_t newRootPhysicalAddress, const char* name,
                      Thread* currentThread) {
    if (newRootPhysicalAddress == 0 || currentThread == nullptr ||
        currentThread->getTask() != this || this == kernelTask) {
        return false;
    }
    // Once sibling threads are detached, their ownership and the new root must
    // reach a committed state even if another CPU kills this task. Interrupts
    // remain enabled for shootdowns, but no blocking/scheduling is allowed here.
    class CommitGuard {
       public:
        CommitGuard() { cpu::disablePreemption(); }
        ~CommitGuard() { cpu::enablePreemption(); }
    } commitGuard;
    uint64_t oldRoot = 0;
    Thread* retiredThreads = nullptr;
    {
        InterruptSpinLockGuard guard(lock_);
        if (!acceptsThreads_ || state_ == TaskState::Terminated ||
            currentThread->getState() == ThreadState::Terminated) return false;
        for (Thread* thread = threads_; thread != nullptr;) {
            Thread* next = thread->getTaskNext();
            if (thread != currentThread &&
                thread->getState() != ThreadState::Terminated) {
                schedule::terminateThread(thread);
                smp::requestRemoteReschedule(
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
        currentThread->setWaitNext(nullptr);
        threads_          = currentThread;
        mainThread_       = currentThread;
        threadCount_      = 1;
        acceptsThreads_   = false;
        state_            = TaskState::Running;
        stopped_          = false;
        stopSignal_       = 0;
        stopReported_     = false;
        continuedPending_ = false;
        pendingSignals_   = 0;
        serviceRole_      = os::ServiceRole::None;
        for (uint32_t signal = 1; signal <= maximumSignal; ++signal) {
            if (signalActions_[signal].getHandler() != signalIgnore) {
                signalActions_[signal] = {};
            }
        }
        copyName(name_, name);
    }
    schedule::quiesceTask(this);
    device::ResourceDomain::ownerExiting(*this);
    for (Thread* thread = retiredThreads; thread; thread = thread->getTaskNext()) {
        Endpoint::cancelThread(thread);
        thread->cancelWait();
        ThreadCleanup::releaseAll(*thread);
    }
    while (retiredThreads) {
        Thread* thread = retiredThreads;
        retiredThreads = thread->getTaskNext();
        if (thread->getKernelStack()) heap::kernelFree(thread->getKernelStack());
        heap::kernelFree(thread);
    }
    memory::PageMappings::releaseTask(*this);
    SharedMemory::releaseTaskMappings(*this);
    {
        PreemptionSpinLockGuard guard(addressSpace_.lock());
        oldRoot = addressSpace_.replaceRoot(newRootPhysicalAddress);
        memory::switchAddressSpace(newRootPhysicalAddress);
    }
    handles_.closeOnExec();
    memory::destroyAddressSpaceRoot(oldRoot);
    device::ResourceDomain::addressSpaceReleased(*this);
    {
        InterruptSpinLockGuard guard(lock_);
        acceptsThreads_ = state_ != TaskState::Terminated;
    }
    return true;
}

int Task::waitForChild(int64_t taskId, Thread* waiter, int& status,
                       uint32_t options, uint64_t& childId) {
    if (waiter == nullptr) return -1;
    TaskReference reaped;
    bool found   = false;
    {
        InterruptSpinLockGuard guard(lock_);
        Task** link = &firstChild_;
        while (*link != nullptr) {
            Task* child = *link;
            const bool matches =
                taskId == -1 ||
                (taskId > 0 && child->id_ == static_cast<uint64_t>(taskId)) ||
                (taskId == 0 && child->taskGroupId_ == taskGroupId_) ||
                (taskId < -1 &&
                 child->taskGroupId_ == static_cast<uint64_t>(-taskId));
            if (!matches) {
                link = &child->nextSibling_;
                continue;
            }
            found           = true;
            bool terminated = false;
            bool stateEvent = false;
            {
                InterruptSpinLockGuard childGuard(child->lock_);
                terminated = child->state_ == TaskState::Terminated;
                if (terminated) {
                    status     = child->exitCode_ >= 0
                                     ? (child->exitCode_ & 0xff) << 8
                                     : ((-child->exitCode_) & 0x7f);
                    stateEvent = true;
                } else if ((options & 2U) != 0 && child->stopped_ &&
                           !child->stopReported_) {
                    child->stopReported_ = true;
                    status = (static_cast<int>(child->stopSignal_) << 8) | 0x7f;
                    stateEvent = true;
                } else if ((options & 8U) != 0 && child->continuedPending_) {
                    child->continuedPending_ = false;
                    status                   = 0xffff;
                    stateEvent               = true;
                }
            }
            if (terminated) {
                *link               = child->nextSibling_;
                child->nextSibling_ = nullptr;
                reaped              = TaskReference(child);
                childId             = child->id_;
                // Hand cleanup to the worker before the waiter can be killed.
                // The local reference and queue reference replace list ownership.
                TaskReclaimer::submit(*child);
                {
                    InterruptSpinLockGuard globalGuard(taskListLock);
                    Task** globalLink = &taskList;
                    while (*globalLink && *globalLink != child)
                        globalLink = &(*globalLink)->nextGlobal_;
                    if (*globalLink == child) *globalLink = child->nextGlobal_;
                    child->nextGlobal_ = nullptr;
                }
                child->release();
                break;
            }
            if (stateEvent) {
                childId = child->id_;
                return 0;
            }
            link = &child->nextSibling_;
        }
        if (reaped == nullptr && found && (options & 1U) == 0) {
            Thread::prepareWait();
            waiter->registerWait(lock_, childWaiters_);
            waiter->setWaitNext(childWaiters_);
            childWaiters_ = waiter;
        }
    }
    if (reaped != nullptr) {
        reaped->destroy();
        return 0;
    }
    if (!found) return -1;
    if ((options & 1U) != 0) {
        childId = 0;
        status  = 0;
        return 2;
    }
    return 1;
}

void Task::notifyChildStateChange() {
    Thread* waiters = nullptr;
    {
        InterruptSpinLockGuard guard(lock_);
        waiters       = childWaiters_;
        childWaiters_ = nullptr;
    }
    while (waiters != nullptr) {
        Thread* waiter = waiters;
        waiters        = waiters->getWaitNext();
        waiter->setWaitNext(nullptr);
        waiter->clearWaitRegistration();
        Thread::wake(waiter);
    }
}

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
        mainThread_ = thread;
    }
    state_ = TaskState::Running;
    return true;
}

int Task::waitForThread(uint64_t threadId, Thread* waiter, int& exitCode,
                        bool noHang) {
    if (waiter == nullptr || waiter->getId() == threadId) {
        return -1;
    }
    InterruptSpinLockGuard guard(lock_);
    Thread* target = threads_;
    while (target != nullptr && target->getId() != threadId) {
        target = target->getTaskNext();
    }
    if (target == nullptr || target->isJoinConsumed()) {
        return -1;
    }
    if (target->getState() == ThreadState::Terminated) {
        exitCode = target->getExitCode();
        target->setJoinConsumed(true);
        if (target->isStackRetired()) reapJoinedThreadLocked(target);
        return 0;
    }
    if (noHang) {
        exitCode = 0;
        return 2;
    }
    Thread::prepareWait();
    waiter->registerWait(lock_, target->accessJoinWaiters(), &target->accessJoinWaitersTail());
    waiter->setWaitNext(nullptr);
    if (target->getJoinWaitersTail() == nullptr) {
        target->setJoinWaiters(waiter);
    } else {
        target->getJoinWaitersTail()->setWaitNext(waiter);
    }
    target->setJoinWaitersTail(waiter);
    exitCode = 0;
    return 1;
}

void Task::reapJoinedThreadLocked(Thread* thread) {
    Thread** link = &threads_;
    while (*link && *link != thread) link = &(*link)->accessTaskNext();
    if (!*link) return;
    *link = thread->getTaskNext();
    if (mainThread_ == thread) mainThread_ = threads_;
    heap::kernelFree(thread);
}

void Task::retireJoinedThread(Thread* thread) {
    InterruptSpinLockGuard guard(lock_);
    thread->setStackRetired(true);
    if (thread->isJoinConsumed()) reapJoinedThreadLocked(thread);
}

void Task::notifyThreadExit(Thread* thread) {
    Thread* waiters     = nullptr;
    bool taskTerminated = false;
    {
        InterruptSpinLockGuard guard(lock_);
        waiters = thread->getJoinWaiters();
        thread->setJoinWaiters(nullptr);
        thread->setJoinWaitersTail(nullptr);
        if (!thread->isExitAccounted() && threadCount_ != 0) {
            --threadCount_;
        }
        const bool newlyExited = !thread->isExitAccounted();
        thread->setExitAccounted(true);
        if (newlyExited && threadCount_ == 0 && this != kernelTask) {
            state_          = TaskState::Terminated;
            acceptsThreads_ = false;
            exitCode_       = thread->getExitCode();
            // The reclaimer detaches endpoint ownership before closing handles.
            taskTerminated = true;
        }
    }
    while (waiters != nullptr) {
        Thread* waiter = waiters;
        waiters        = waiters->getWaitNext();
        waiter->setWaitNext(nullptr);
        waiter->clearWaitRegistration();
        Thread::wake(waiter);
    }
    if (taskTerminated) {
        TaskReclaimer::submit(*this);
    }
}

void Task::terminateOtherThreads(int exitCode) {
    InterruptSpinLockGuard guard(lock_);
    acceptsThreads_ = false;
    exitCode_       = exitCode;
    for (Thread* thread = threads_; thread != nullptr;
         thread         = thread->getTaskNext()) {
        if (thread != Thread::getCurrent() &&
            thread->getState() != ThreadState::Terminated) {
            schedule::terminateThread(thread);
            thread->setExitAccounted(true);
            smp::requestRemoteReschedule(
                static_cast<uint32_t>(thread->getCpuId()));
            if (threadCount_ != 0) {
                --threadCount_;
            }
        }
    }
    if (threadCount_ == 0) {
        state_ = TaskState::Terminated;
        TaskReclaimer::submit(*this);
    }
}

void Task::destroy() {
    if (this == kernelTask || this == task::getCurrent()) {
        return;
    }
    TaskReference lifetime(this);
    TaskReclaimer::submit(*this);
    const uint64_t flags = interrupt::disable();
    interrupt::enable();
    // No stack pointer from this waiter is stored in the reclaimer. Killing or
    // cancelling it cannot strand the queue or its owned task reference.
    while (__atomic_load_n(&resourceState_, __ATOMIC_ACQUIRE) != 2U)
        schedule::yieldThread();
    interrupt::restore(flags);
}

bool Task::destroyResources() {
    uint64_t root;
    bool discard;
    {
        InterruptSpinLockGuard guard(lock_);
        discard         = state_ == TaskState::Created && threadCount_ == 0;
        acceptsThreads_ = false;
        for (Thread* thread = threads_; thread != nullptr;
             thread         = thread->getTaskNext()) {
            if (thread->getState() != ThreadState::Terminated) {
                schedule::terminateThread(thread);
            }
        }
        threadCount_ = 0;
        state_       = TaskState::Terminated;
    }
    schedule::quiesceTask(this);
    device::ResourceDomain::ownerExiting(*this);
    {
        PreemptionSpinLockGuard guard(addressSpace_.lock());
        root = addressSpace_.replaceRoot(0);
    }
    Endpoint::closeOwner(this);
    for (Thread* thread = threads_; thread; thread = thread->getTaskNext()) {
        Endpoint::cancelThread(thread);
        thread->cancelWait();
        ThreadCleanup::releaseAll(*thread);
    }
    adoptChildren();
    for (Thread* thread = threads_; thread;) {
        Thread* next = thread->getTaskNext();
        if (thread->getKernelStack()) heap::kernelFree(thread->getKernelStack());
        heap::kernelFree(thread);
        thread = next;
    }
    threads_ = nullptr;
    mainThread_ = nullptr;
    handles_.dispose();
    memory::PageMappings::releaseTask(*this);
    SharedMemory::releaseTaskMappings(*this);
    if (root != 0) memory::destroyAddressSpaceRoot(root);
    device::ResourceDomain::addressSpaceReleased(*this);
    if (discard) {
        if (parent_ != nullptr) {
            InterruptSpinLockGuard parentGuard(parent_->lock_);
            Task** link = &parent_->firstChild_;
            while (*link != nullptr && *link != this) {
                link = &(*link)->nextSibling_;
            }
            if (*link == this) *link = nextSibling_;
            nextSibling_ = nullptr;
        }
        InterruptSpinLockGuard globalGuard(taskListLock);
        Task** link = &taskList;
        while (*link != nullptr && *link != this) {
            link = &(*link)->nextGlobal_;
        }
        if (*link == this) *link = nextGlobal_;
        nextGlobal_ = nullptr;
    }
    if (discard) release();
    return !discard;
}

void Task::grantCapabilities(uint64_t capabilities) {
    capabilities_ |= capabilities;
}

bool Task::hasCapability(uint64_t capability) const {
    return (capabilities_ & capability) == capability;
}

void Task::setBootstrapHandle(uint64_t handle) { bootstrapHandle_ = handle; }

uint64_t Task::bootstrapHandle() const { return bootstrapHandle_; }

void Task::setStandardHandle(uint32_t index, uint64_t handle) {
    if (index < 3) {
        standardHandles_[index] = handle;
    }
}

uint64_t Task::standardHandle(uint32_t index) const {
    return index < 3 ? standardHandles_[index] : 0;
}

uint64_t Task::taskGroupId() const { return taskGroupId_; }

uint64_t Task::sessionId() const { return sessionId_; }
void Task::setCommandLine(uint32_t count, const char* const* arguments) {
    size_t used = 0;
    for (uint32_t i = 0; i < count && used + 1 < sizeof(commandLine_); ++i) {
        const char* text = arguments ? arguments[i] : nullptr;
        if (!text) continue;
        constexpr char prefix[] = "--os-terminal=";
        size_t p = 0;
        while (prefix[p] && prefix[p] == text[p]) ++p;
        if (!prefix[p] && text[p] >= '1' && text[p] <= '7' && !text[p + 1]) {
            continue;
        }
        while (*text && used + 1 < sizeof(commandLine_)) commandLine_[used++] = *text++;
        commandLine_[used++] = 0;
    }
    commandLineSize_ = used;
}

bool Task::setTaskGroup(uint64_t taskGroupId) {
    if (taskGroupId == 0) taskGroupId = id_;
    auto leader = task::find(taskGroupId);
    if (leader == nullptr || leader->sessionId() != sessionId_) return false;
    InterruptSpinLockGuard guard(lock_);
    if (leader->sessionId() != sessionId_) return false;
    taskGroupId_ = taskGroupId;
    return true;
}

bool Task::createSession() {
    InterruptSpinLockGuard guard(lock_);
    if (taskGroupId_ == id_) {
        return false;
    }
    sessionId_   = id_;
    if (authenticationSession_) authenticationSession_ = id_;
    taskGroupId_ = id_;
    terminalKind_ = terminalId_ = 0;
    return true;
}

bool Task::setMemoryFrozen(bool frozen) {
    if (managedService_ || this == kernelTask || this == task::getCurrent()) return false;
    {
        InterruptSpinLockGuard guard(lock_);
        if (state_ == TaskState::Terminated) return false;
        if (frozen && !memoryFrozen_) memoryFreezeDeadline_ = time::getMonotonicTime() + 20000000000ULL;
        if (!frozen) memoryFreezeDeadline_ = 0;
        __atomic_store_n(&memoryFrozen_, frozen, __ATOMIC_RELEASE);
        for (Thread* thread = threads_; thread; thread = thread->getTaskNext()) {
            if (frozen) {
                if (thread->getState() == ThreadState::Stopped || thread->getState() == ThreadState::Terminated) continue;
                thread->setStateBeforeStop(thread->getState());
                schedule::dequeueThread(thread); thread->setState(ThreadState::Stopped);
                smp::requestRemoteReschedule(static_cast<uint32_t>(thread->getCpuId()));
            } else if (!stopped_ && thread->getState() == ThreadState::Stopped) {
                thread->setState(thread->getStateBeforeStop());
                if (thread->getState() == ThreadState::Ready || thread->getState() == ThreadState::Running) {
                    thread->setState(ThreadState::Ready); schedule::enqueueThread(thread);
                }
            }
        }
    }
    if (frozen) schedule::quiesceTask(this);
    return true;
}

bool Task::claimMemoryFreezeExpiry(uint64_t now) {
    InterruptSpinLockGuard guard(lock_);
    if (!memoryFrozen_ || !memoryFreezeDeadline_ || now < memoryFreezeDeadline_ || state_ == TaskState::Terminated) return false;
    memoryFreezeDeadline_ = 0; return true;
}
void Task::authentication(uint64_t& session, uint32_t& uid, uint32_t& gid) {
    InterruptSpinLockGuard guard(lock_);
    session = authenticationSession_; uid = authenticationUid_; gid = authenticationGid_;
}
bool Task::authenticate(uint32_t uid, uint32_t gid, bool preserveCredentials) {
    InterruptSpinLockGuard guard(lock_);
    if (state_ == TaskState::Terminated || managedService_ || uid >= 65535 || gid >= 65535 ||
        (preserveCredentials && (effectiveUid_ != uid || effectiveGid_ != gid))) return false;
    if (!preserveCredentials) { uid_ = effectiveUid_ = uid; gid_ = effectiveGid_ = gid; }
    authenticationSession_ = sessionId_; authenticationUid_ = uid; authenticationGid_ = gid;
    return true;
}

bool Task::setSignalAction(uint32_t signal, const SignalAction& action,
                           SignalAction* oldAction) {
    if (!signal::isValidSignal(signal) ||
        signal == static_cast<uint32_t>(Signal::Kill) ||
        signal == static_cast<uint32_t>(Signal::Stop)) {
        return false;
    }
    InterruptSpinLockGuard guard(lock_);
    if (oldAction != nullptr) *oldAction = signalActions_[signal];
    signalActions_[signal] = action;
    if (action.getHandler() == signalIgnore) {
        pendingSignals_ &= ~(1ULL << (signal - 1));
    }
    return true;
}

bool Task::getSignalAction(uint32_t signal, SignalAction& action) {
    if (!signal::isValidSignal(signal)) return false;
    InterruptSpinLockGuard guard(lock_);
    action = signalActions_[signal];
    return true;
}

uint64_t Task::pendingSignals(Thread* thread) {
    InterruptSpinLockGuard guard(lock_);
    return pendingSignals_ |
           (thread == nullptr ? 0 : thread->getPendingSignals());
}

uint32_t Task::takePendingSignal(Thread* thread, uint64_t accepted,
                                 SignalAction& action, bool respectMask) {
    if (thread == nullptr) return 0;
    InterruptSpinLockGuard guard(lock_);
    uint64_t available =
        (pendingSignals_ | thread->getPendingSignals()) & accepted;
    if (respectMask) available &= ~thread->getSignalMask();
    if (available == 0) return 0;
    const uint32_t signal =
        static_cast<uint32_t>(__builtin_ctzll(available)) + 1;
    const uint64_t bit = 1ULL << (signal - 1);
    if ((thread->getPendingSignals() & bit) != 0)
        thread->setPendingSignals(thread->getPendingSignals() & (~bit));
    else
        pendingSignals_ &= ~bit;
    action = signalActions_[signal];
    if ((action.getFlags() & signalFlagResetHand) != 0) {
        signalActions_[signal] = {};
    }
    return signal;
}

bool Task::deliverThreadSignal(uint64_t threadId, uint32_t signal) {
    if (signal > maximumSignal || this == kernelTask || isTerminated()) {
        return false;
    }
    {
        InterruptSpinLockGuard guard(lock_);
        Thread* target = threads_;
        while (target != nullptr && target->getId() != threadId) {
            target = target->getTaskNext();
        }
        if (target == nullptr ||
            target->getState() == ThreadState::Terminated) {
            return false;
        }
        if (signal == 0) return true;
        const SignalAction action = signalActions_[signal];
        if (action.getHandler() == signalIgnore) return true;
        if (action.getHandler() != signalDefault) {
            target->setPendingSignals(target->getPendingSignals() |
                                      (1ULL << (signal - 1)));
            if (target != Thread::getCurrent() &&
                (target->getSignalMask() & (1ULL << (signal - 1))) == 0)
                Endpoint::interruptThread(target);
            return true;
        }
    }
    return deliverSignal(signal);
}

bool Task::deliverSignal(uint32_t signal) {
    if (!signal::isValidSignal(signal) || this == kernelTask ||
        isTerminated()) {
        return false;
    }
    SignalAction action{};
    bool queued = false;
    {
        InterruptSpinLockGuard guard(lock_);
        action = signalActions_[signal];
        if (action.getHandler() == signalIgnore) return true;
        bool allBlocked = true;
        for (Thread* thread = threads_; thread != nullptr;
             thread         = thread->getTaskNext()) {
            if (thread->getState() != ThreadState::Terminated &&
                (thread->getSignalMask() & (1ULL << (signal - 1))) == 0) {
                allBlocked = false;
                break;
            }
        }
        queued = signal != static_cast<uint32_t>(Signal::Kill) &&
                 signal != static_cast<uint32_t>(Signal::Stop) &&
                 (action.getHandler() != signalDefault || allBlocked);
        if (queued) {
            pendingSignals_ |= 1ULL << (signal - 1);
            for (Thread* thread = threads_; thread; thread = thread->getTaskNext()) {
                if (thread != Thread::getCurrent() && thread->getState() != ThreadState::Terminated &&
                    (thread->getSignalMask() & (1ULL << (signal - 1))) == 0)
                    Endpoint::interruptThread(thread);
            }
        }
    }
    if (queued) return true;
    if (signal == static_cast<uint32_t>(Signal::WindowChanged) ||
        signal == static_cast<uint32_t>(Signal::Child) ||
        signal == static_cast<uint32_t>(Signal::Urgent))
        return true;
    if (signal == static_cast<uint32_t>(Signal::Continue)) {
        {
            InterruptSpinLockGuard guard(lock_);
            stopped_          = false;
            continuedPending_ = true;
            for (Thread* thread = threads_; thread != nullptr;
                 thread         = thread->getTaskNext()) {
                if (memoryFrozen()) continue;
                if (thread->getState() != ThreadState::Stopped) continue;
                thread->setState(thread->getStateBeforeStop());
                if (thread->getState() == ThreadState::Ready ||
                    thread->getState() == ThreadState::Running) {
                    thread->setState(ThreadState::Ready);
                    schedule::enqueueThread(thread);
                }
            }
        }
        {
            auto parent = parentReference();
            if (parent) {
                parent->notifyChildStateChange();
                parent->deliverSignal(static_cast<uint32_t>(Signal::Child));
            }
        }
        return true;
    }
    if (signal == static_cast<uint32_t>(Signal::Stop) ||
        signal == static_cast<uint32_t>(Signal::TerminalStop) ||
        signal == static_cast<uint32_t>(Signal::TerminalInput) ||
        signal == static_cast<uint32_t>(Signal::TerminalOutput)) {
        {
            InterruptSpinLockGuard guard(lock_);
            stopped_          = true;
            stopSignal_       = signal;
            stopReported_     = false;
            continuedPending_ = false;
            for (Thread* thread = threads_; thread != nullptr;
                 thread         = thread->getTaskNext()) {
                if (thread->getState() == ThreadState::Terminated ||
                    thread->getState() == ThreadState::Stopped)
                    continue;
                thread->setStateBeforeStop(thread->getState());
                schedule::dequeueThread(thread);
                thread->setState(ThreadState::Stopped);
                smp::requestRemoteReschedule(
                    static_cast<uint32_t>(thread->getCpuId()));
            }
        }
        {
            auto parent = parentReference();
            if (parent) {
                parent->notifyChildStateChange();
                parent->deliverSignal(static_cast<uint32_t>(Signal::Child));
            }
        }
        return true;
    }
    terminateOtherThreads(-static_cast<int>(signal));
    if (this == task::getCurrent()) {
        Thread::exitCurrent(-static_cast<int>(signal));
    }
    destroy();
    return true;
}

void task::init() {
    taskListLock.init();
    taskList      = nullptr;
    void* storage = heap::kernelAllocate(sizeof(Task));
    Task* task    = storage == nullptr ? nullptr : new (storage) Task;
    if (task == nullptr ||
        !task->init("kernel", memory::getKernelAddressSpaceRoot(), nullptr)) {
        serial::serialWrite("[TASK] unable to initialize kernel task\n");
        cpu::halt();
    }
    kernelTask = task;
    taskList   = task;
    kernelTask->attachThread(Thread::getCurrent());
    cpu::getCurrent()->setCurrentTask(kernelTask);
    TaskReclaimer::init();
    device::ResourceDomain::initialize();
    ServiceRecovery::initialize();
    serial::serialWrite("[TASK] kernel task initialized\n");
}

Task* task::getCurrent() { return cpu::getCurrent()->getCurrentTask(); }

void Task::setCredentials(uint32_t uid, uint32_t gid, bool effectiveOnly) {
    InterruptSpinLockGuard guard(lock_);
    effectiveUid_ = uid;
    effectiveGid_ = gid;
    if (!effectiveOnly) {
        if (uid_ != uid || gid_ != gid) authenticationSession_ = 0;
        uid_ = uid; gid_ = gid;
    }
}

Task* task::getIdle() { return kernelTask; }

TaskReference task::find(uint64_t taskId) {
    InterruptSpinLockGuard guard(taskListLock);
    for (Task* task = taskList; task != nullptr; task = task->nextGlobal_) {
        if (task->id() == taskId) return task;
    }
    return nullptr;
}

bool signal::isValidSignal(uint32_t signal) {
    return signal >= 1 && signal <= maximumSignal;
}

TaskReference task::at(uint32_t index) {
    InterruptSpinLockGuard guard(taskListLock);
    for (Task* task = taskList; task; task = task->nextGlobal_) {
        if (task == kernelTask) continue;
        if (!index--) return task;
    }
    return nullptr;
}

TaskReference task::next(uint64_t minimumId) {
    InterruptSpinLockGuard guard(taskListLock);
    Task* selected = nullptr;
    for (Task* task = taskList; task; task = task->nextGlobal_)
        if (task != kernelTask && task->id() >= minimumId && (!selected || task->id() < selected->id())) selected = task;
    return selected;
}

uint64_t task::terminalForeground(uint32_t kind, uint32_t index) {
    return kind >= 1 && kind <= 2 && index < 8 ? __atomic_load_n(&terminalForegroundGroups[kind - 1][index], __ATOMIC_ACQUIRE) : 0;
}
bool task::setTerminalForeground(uint32_t kind, uint32_t index, uint64_t group) {
    if (kind < 1 || kind > 2 || index >= (kind == 1 ? 6U : 8U)) return false;
    __atomic_store_n(&terminalForegroundGroups[kind - 1][index], group, __ATOMIC_RELEASE);
    return true;
}

bool signal::sendSignalToTask(uint64_t taskId, uint32_t signal, Task* authenticatedCaller) {
    auto task = task::find(taskId);
    Task* caller = authenticatedCaller ? authenticatedCaller : task::getCurrent();
    if (task && signal && task->managedService()) return false;
    if (task && caller && caller->effectiveUid() != 0 && caller->uid() != task->uid() &&
        caller->effectiveUid() != task->uid()) return false;
    return task != nullptr && (signal == 0 ? !task->isTerminated() : task->deliverSignal(signal));
}

bool signal::sendSignalToTaskGroup(uint64_t taskGroupId, uint32_t signal, Task* authenticatedCaller) {
    if (taskGroupId == 0 || (signal != 0 && !signal::isValidSignal(signal))) return false;
    bool delivered = false;
    uint64_t before = UINT64_MAX;
    while (true) {
        TaskReference batch[32];
        uint32_t count = 0;
        {
            InterruptSpinLockGuard guard(taskListLock);
            for (Task* task = taskList; task && count < 32; task = task->nextGlobal_) {
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
            Task* caller = authenticatedCaller ? authenticatedCaller : task::getCurrent();
            const bool permitted = !caller || caller->effectiveUid() == 0 || caller->uid() == task->uid() ||
                                   caller->effectiveUid() == task->uid();
            if (permitted && (!signal || !task->managedService()) && task->taskGroupId() == taskGroupId)
                delivered = (signal == 0 ? !task->isTerminated() : task->deliverSignal(signal)) || delivered;
        }
    }
    return delivered;
}

[[noreturn]] void task::exitCurrent(int exitCode) {
    Task* task = task::getCurrent();
    if (task == nullptr || task == kernelTask) {
        Thread::exitCurrent(exitCode);
    }
    task->terminateOtherThreads(exitCode);
    Thread::exitCurrent(exitCode);
}

}  // namespace kernel
