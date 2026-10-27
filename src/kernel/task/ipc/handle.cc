/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/ipc/handle.cc
 * Generation-tagged handles, shared references, and transactional transfer.
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/task.h"

namespace kernel::task {
namespace {
// Zero newly allocated table storage before any entry can be inspected.
void clearMemory(void* address, uint64_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(address);
    for (uint64_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}
}  // namespace

// Create an unpublished table. owner identifies receiver-close effects; escrow
// marks recovery-domain references that must not count as live client
// references. Allocation failure leaves capacity zero.
bool HandleTable::init(uint32_t capacity, Task* owner, bool escrow) {
    owner_    = owner;
    escrow_   = escrow;
    disposed_ = false;
    capacity_ = 0;
    lock_.init();
    entries_ = static_cast<HandleEntry*>(
        mm::allocateHeap(sizeof(HandleEntry) * capacity));
    if (entries_ == nullptr) {
        return false;
    }
    clearMemory(entries_, sizeof(HandleEntry) * capacity);
    capacity_ = capacity;
    lock_.init();
    return true;
}

// Current slot capacity, read under the table lock.
uint32_t HandleTable::capacity() {
    InterruptSpinLockGuard guard(lock_);
    return capacity_;
}

// Caller holds lock_. Allocate and copy a larger array before replacing
// entries_, so allocation failure preserves every existing slot. Entry
// addresses are internal and must not be retained across table growth.
bool HandleTable::growLocked(uint32_t required) {
    if (disposed_) return false;
    uint32_t capacity = capacity_;
    while (capacity < required) {
        if (capacity == 0 || capacity > UINT32_MAX / 2) return false;
        capacity *= 2;
    }
    if (capacity == capacity_) return true;
    auto* entries = static_cast<HandleEntry*>(
        mm::allocateHeap(sizeof(HandleEntry) * capacity));
    if (!entries) return false;
    clearMemory(entries, sizeof(HandleEntry) * capacity);
    for (uint32_t i = 0; i < capacity_; ++i) entries[i] = entries_[i];
    mm::freeHeap(entries_);
    entries_  = entries;
    capacity_ = capacity;
    return true;
}

// Wrap object in a new shared Reference and install one slot. Task and
// Endpoint take their own object-level reference here; other handle types use
// the ownership supplied by their creator. Return zero on failure and undo
// only references acquired by this function.
uint64_t HandleTable::add(void* object, HandleType type, uint32_t rights) {
    if (object == nullptr || type == HandleType::None) {
        return 0;
    }
    auto* reference =
        static_cast<Reference*>(mm::allocateHeap(sizeof(Reference)));
    if (!reference) return 0;
    new (reference) Reference(object, type, 1);
    if (type == HandleType::Task) static_cast<Task*>(object)->retain();
    if (type == HandleType::Endpoint) static_cast<Endpoint*>(object)->retain();
    InterruptSpinLockGuard guard(lock_);
    const uint64_t result = installLocked(reference, rights, 0);
    if (!result) {
        if (type == HandleType::Task) static_cast<Task*>(object)->release();
        if (type == HandleType::Endpoint)
            static_cast<Endpoint*>(object)->release();
        mm::freeHeap(reference);
    }
    return result;
}

// Consume a caller-supplied reference into one free slot under lock_. Advance
// the slot generation (skipping zero) before encoding the handle: low 32 bits
// are index + 1, bits 32..47 are generation. A failed install does not consume
// the reference; the caller must release it.
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
        entry.flags     = flags;
        entry.setObject(reference->object);
        entry.setRights(rights);
        entry.setType(static_cast<uint16_t>(reference->type));
        return (static_cast<uint64_t>(entry.getGeneration()) << 32) |
               (index + 1);
    }
    return 0;
}

// Install a fresh reference at an explicit slot/generation, preserving handle
// values for controlled inheritance. Reject occupied slots instead of silently
// replacing another object.
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
    auto* reference =
        static_cast<Reference*>(mm::allocateHeap(sizeof(Reference)));
    if (!reference) return false;
    new (reference) Reference(object, type, 1);
    if (type == HandleType::Task) static_cast<Task*>(object)->retain();
    if (type == HandleType::Endpoint) static_cast<Endpoint*>(object)->retain();
    entry.reference = reference;
    entry.flags     = 0;
    entry.setObject(object);
    entry.setRights(rights);
    entry.setType(static_cast<uint16_t>(type));
    entry.setGeneration(generation);
    return true;
}

// Borrow an object after type, generation and rights checks. The lock is
// released on return; use Pin if object lifetime must span later work.
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

// Return borrowed object/type/rights metadata for a handle. This is not a
// lifetime pin and requires a retained reference for later dereference.
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

// Inspect a table position and reconstruct its current generation-tagged
// handle. Empty slots are skipped by callers performing inheritance or
// controlled enumeration; positions are not persistent object identities.
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

// Detach the slot under lock_, then perform receiver-close notification and
// final-reference cleanup outside it. Destructors can enter endpoints or
// resource domains and must not inherit the table lock.
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
        if (escrow_)
            __atomic_sub_fetch(&reference->escrow, 1U, __ATOMIC_ACQ_REL);
        entry.reference = nullptr;
        entry.flags     = 0;
        entry.setObject(nullptr);
        entry.setRights(0);
        entry.setType(static_cast<uint16_t>(HandleType::None));
    }
    if (reference->type == HandleType::Endpoint && owner_ &&
        !hasObjectRights(reference->object, HandleType::Endpoint,
                         handleRightRead))
        static_cast<Endpoint*>(reference->object)->receiverClosed(owner_);
    (reference)->release();
    return true;
}

// Hold the shared handle reference, independently of how many slots in any one
// process currently refer to it.
void HandleTable::Reference::retain() {
    auto* reference = this;
    __atomic_add_fetch(&reference->count, 1U, __ATOMIC_RELAXED);
}

// Validate and retain while still holding the table lock, closing the gap
// between lookup and concurrent close. ThreadCleanup releases the pin on
// normal destruction or forced thread teardown.
HandleTable::Pin::Pin(HandleTable& table, uint64_t handle, HandleType type,
                      uint32_t rights) {
    InterruptSpinLockGuard guard(table.lock_);
    if (!table.validLocked(handle)) return;
    const auto& entry = table.entries_[static_cast<uint32_t>(handle) - 1];
    if (entry.reference->type != type || (entry.getRights() & rights) != rights)
        return;
    reference_ = entry.reference;
    (reference_)->retain();
    cleanup_.attach(reference_, [](void* value) {
        (static_cast<Reference*>(value))->release();
    });
}
// The cleanup member releases the retained Reference.
HandleTable::Pin::~Pin() = default;

// Only the last shared reference performs type-specific object cleanup.
// Task/Endpoint/PageObject have their own lifetime counters; device objects
// release resources through their owner-domain methods rather than being
// blindly freed as raw storage. Finally free the Reference wrapper itself.
void HandleTable::Reference::release() {
    auto* reference = this;
    if (__atomic_sub_fetch(&reference->count, 1U, __ATOMIC_ACQ_REL) != 0)
        return;
    void* object          = reference->object;
    const HandleType type = reference->type;
    if (type == HandleType::SharedMemory) {
        ::kernel::mm::sharedMemory().closeHandleObject(object);
    } else if (type == HandleType::PageObject) {
        static_cast<mm::PageObject*>(object)->release();
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
    } else if (type == HandleType::FramebufferMapping) {
        static_cast<device::FrameMapping*>(object)->destroy();
    } else if (type == HandleType::DeviceDomain) {
        static_cast<device::ResourceDomain*>(object)->closeLease();
    }
    mm::freeHeap(reference);
}

// Reject high bits outside the generation encoding and require both the
// current generation and an occupied slot. Caller already holds lock_.
bool HandleTable::validLocked(uint64_t handle) const {
    const uint32_t slot = static_cast<uint32_t>(handle);
    return slot && slot <= capacity_ && (handle >> 48) == 0 &&
           entries_[slot - 1].getObject() &&
           entries_[slot - 1].getGeneration() == (handle >> 32);
}

// Share the existing Reference in a new local slot. rights must be a subset
// and flags must be supported. If slot allocation fails, undo the extra
// reference without disturbing the original handle.
uint64_t HandleTable::duplicate(uint64_t handle, uint32_t rights,
                                uint32_t flags) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle) || (flags & ~os::handleFlagsMask)) return 0;
    HandleEntry& entry = entries_[static_cast<uint32_t>(handle) - 1];
    if ((rights & entry.getRights()) != rights) return 0;
    Reference* reference = entry.reference;
    (reference)->retain();
    const uint64_t result = installLocked(reference, rights, flags);
    if (!result) (reference)->release();
    return result;
}

// Irreversibly narrow this slot's rights; other slots sharing the object keep
// their own independent rights.
bool HandleTable::restrictRights(uint64_t handle, uint32_t rights) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle)) return false;
    HandleEntry& entry = entries_[static_cast<uint32_t>(handle) - 1];
    if ((rights & entry.getRights()) != rights) return false;
    entry.setRights(rights);
    return true;
}

// Copy a reference across tables, optionally preserving a handle number. Lock
// the two tables in address order so opposite-direction copies cannot
// deadlock. Escrow destinations also increment the hidden escrow count.
uint64_t HandleTable::copyTo(HandleTable& destination, uint64_t handle,
                             uint64_t destinationHandle) {
    if (&destination == this) {
        void* object;
        HandleType type;
        uint32_t rights;
        return inspect(handle, &object, &type, &rights)
                   ? duplicate(handle, rights)
                   : 0;
    }
    HandleTable* first  = reinterpret_cast<uintptr_t>(this) <
                                 reinterpret_cast<uintptr_t>(&destination)
                              ? this
                              : &destination;
    HandleTable* second = first == this ? &destination : this;
    InterruptSpinLockGuard firstGuard(first->lock_);
    InterruptSpinLockGuard secondGuard(second->lock_);
    if (destination.disposed_ || !validLocked(handle)) return 0;
    auto& source = entries_[static_cast<uint32_t>(handle) - 1];
    if (destinationHandle) {
        const uint32_t slot       = static_cast<uint32_t>(destinationHandle);
        const uint64_t generation = destinationHandle >> 32;
        if (!slot || !generation || generation > UINT16_MAX ||
            !destination.growLocked(slot) ||
            destination.entries_[slot - 1].getObject())
            return 0;
        auto& entry = destination.entries_[slot - 1];
        entry       = source;
        entry.setGeneration(static_cast<uint16_t>(generation));
        (entry.reference)->retain();
        if (destination.escrow_)
            __atomic_add_fetch(&entry.reference->escrow, 1U, __ATOMIC_RELAXED);
        return destinationHandle;
    }
    (source.reference)->retain();
    const uint64_t result = destination.installLocked(
        source.reference, source.getRights(), source.flags);
    if (result && destination.escrow_)
        __atomic_add_fetch(&source.reference->escrow, 1U, __ATOMIC_RELAXED);
    if (!result) (source.reference)->release();
    return result;
}

// Per-slot inheritance/exec flags; reject bits outside the shared ABI mask.
bool HandleTable::setFlags(uint64_t handle, uint32_t flags) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle) || (flags & ~os::handleFlagsMask)) return false;
    entries_[static_cast<uint32_t>(handle) - 1].flags = flags;
    return true;
}

// Current per-slot flags, subject to generation validation.
bool HandleTable::getFlags(uint64_t handle, uint32_t& flags) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle)) return false;
    flags = entries_[static_cast<uint32_t>(handle) - 1].flags;
    return true;
}

// Search live slots for the same object with every requested right; this is
// how endpoint operations authenticate possession beyond a raw pointer.
bool HandleTable::hasObjectRights(void* object, HandleType type,
                                  uint32_t rights) {
    InterruptSpinLockGuard guard(lock_);
    for (uint32_t i = 0; i < capacity_; ++i) {
        const auto& entry = entries_[i];
        if (entry.getObject() == object &&
            entry.getType() == static_cast<uint16_t>(type) &&
            (entry.getRights() & rights) == rights)
            return true;
    }
    return false;
}
// Report shared references excluding recovery escrow. Keeping an object for
// recovery must not make a server believe ordinary clients still exist.
int64_t HandleTable::references(uint64_t handle) {
    InterruptSpinLockGuard guard(lock_);
    if (!validLocked(handle)) return -2;
    const auto* reference =
        entries_[static_cast<uint32_t>(handle) - 1].reference;
    return static_cast<int64_t>(
               __atomic_load_n(&reference->count, __ATOMIC_ACQUIRE)) -
           __atomic_load_n(&reference->escrow, __ATOMIC_ACQUIRE);
}

// Validate the entire handle vector under the source lock before retaining
// anything. Queue capture owns references, not receiver handle numbers.
// commitMove is true at send/call enqueue; reply defers source-slot removal
// until destination installation succeeds. Failure before capture commits must
// leave every source handle intact.
bool HandleTable::Transfer::capture(HandleTable& source,
                                    const os::IpcMessage& message,
                                    bool commitMove) {
    if (count_ || message.handleCount > 4) return false;
    InterruptSpinLockGuard guard(source.lock_);
    for (uint32_t i = 0; i < message.handleCount; ++i) {
        if (!source.validLocked(message.handles[i])) return false;
        const auto& entry =
            source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
        const auto type = entry.reference->type;
        // Device mappings and bindings cannot escape their owner through
        // generic IPC. Their permission and teardown rules are tied to a
        // specific task.
        if (type == HandleType::MmioRegion || type == HandleType::DmaBuffer ||
            type == HandleType::InterruptBinding ||
            type == HandleType::DeviceDomain ||
            type == HandleType::FramebufferMapping)
            return false;
        if (message.handleOptions[i] &
            ~(os::ipcHandleRestrictRights | os::ipcHandleMove))
            return false;
        if ((message.handleOptions[i] & os::ipcHandleRestrictRights) &&
            (message.handleRights[i] & entry.getRights()) !=
                message.handleRights[i])
            return false;
        for (uint32_t j = 0; j < i; ++j)
            if (message.handles[i] == message.handles[j] &&
                ((message.handleOptions[i] | message.handleOptions[j]) &
                 os::ipcHandleMove))
                return false;
    }
    // All inputs are now valid. Retain each Reference before closing any move
    // source so the queued message remains an owner even if no source slot
    // does.
    for (uint32_t i = 0; i < message.handleCount; ++i) {
        auto& entry =
            source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
        references_[i] = entry.reference;
        rights_[i] = (message.handleOptions[i] & os::ipcHandleRestrictRights)
                         ? message.handleRights[i]
                         : entry.getRights();
        (references_[i])->retain();
    }
    count_ = message.handleCount;
    // Capture is the enqueue commit point. No further fallible step may follow
    // it in send; move leaves the queued reference as the new owner.
    for (uint32_t i = 0; i < count_; ++i) {
        if (!(message.handleOptions[i] & os::ipcHandleMove)) continue;
        if (!commitMove) {
            moveSource_     = &source;
            moveHandles_[i] = message.handles[i];
            continue;
        }
        auto& entry =
            source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
        entry.reference = nullptr;
        entry.setObject(nullptr);
        entry.setRights(0);
        entry.setType(static_cast<uint16_t>(HandleType::None));
        entry.flags = 0;
        (references_[i])->release();
    }
    return true;
}

// Acquire source/destination locks in address order when reply moves still
// need committing. A same-table move needs only one lock. installLocked checks
// all prerequisites before changing slots on either side.
bool HandleTable::Transfer::install(HandleTable& destination,
                                    os::IpcMessage& message) {
    if (!moveSource_ || moveSource_ == &destination) {
        InterruptSpinLockGuard guard(destination.lock_);
        return installLocked(destination, message);
    }
    HandleTable* first  = reinterpret_cast<uintptr_t>(moveSource_) <
                                 reinterpret_cast<uintptr_t>(&destination)
                              ? moveSource_
                              : &destination;
    HandleTable* second = first == moveSource_ ? &destination : moveSource_;
    InterruptSpinLockGuard firstGuard(first->lock_);
    InterruptSpinLockGuard secondGuard(second->lock_);
    return installLocked(destination, message);
}

// Require a live destination and, for deferred moves, unchanged source
// references. Grow enough space for the complete vector before removing
// sources. After that point installation has no expected allocation failure,
// so the receiver never observes only part of one transferred vector.
bool HandleTable::Transfer::installLocked(HandleTable& destination,
                                          os::IpcMessage& message) {
    if (destination.disposed_) return false;
    if (moveSource_) {
        for (uint32_t i = 0; i < count_; ++i)
            if (moveHandles_[i] &&
                (!moveSource_->validLocked(moveHandles_[i]) ||
                 moveSource_
                         ->entries_[static_cast<uint32_t>(moveHandles_[i]) - 1]
                         .reference != references_[i]))
                return false;
    }
    uint32_t free = 0;
    for (uint32_t i = 0; i < destination.capacity_; ++i)
        if (!destination.entries_[i].getObject()) ++free;
    if (free < count_ &&
        !destination.growLocked(destination.capacity_ + count_ - free))
        return false;
    if (moveSource_) {
        for (uint32_t i = 0; i < count_; ++i) {
            if (!moveHandles_[i]) continue;
            auto& entry =
                moveSource_
                    ->entries_[static_cast<uint32_t>(moveHandles_[i]) - 1];
            entry.reference = nullptr;
            entry.setObject(nullptr);
            entry.setRights(0);
            entry.setType(static_cast<uint16_t>(HandleType::None));
            entry.flags = 0;
            (references_[i])->release();
            moveHandles_[i] = 0;
        }
        moveSource_ = nullptr;
    }
    // Replace sender-local values with receiver-local handles. Clearing our
    // reference array transfers ownership to the installed destination slots;
    // clear must not release these successfully installed references again.
    for (uint32_t i = 0; i < count_; ++i) {
        message.handles[i] =
            destination.installLocked(references_[i], rights_[i], 0);
        message.handleRights[i]  = rights_[i];
        message.handleOptions[i] = 0;
        references_[i]           = nullptr;
    }
    message.handleCount = count_;
    count_              = 0;
    return true;
}

// Drop only references still held by this transfer. Deferred reply moves that
// never committed leave their source slots unchanged.
void HandleTable::Transfer::clear() {
    for (uint32_t i = 0; i < count_; ++i) {
        (references_[i])->release();
        references_[i] = nullptr;
    }
    count_      = 0;
    moveSource_ = nullptr;
    for (auto& handle : moveHandles_) handle = 0;
}

// Inspect flags under lock but close outside it, because closing can enter
// other subsystems. The exec commit path controls competing task activity.
void HandleTable::closeOnExec() {
    for (uint32_t i = 0; i < capacity(); ++i) {
        uint64_t handle = 0;
        {
            InterruptSpinLockGuard guard(lock_);
            const auto& entry = entries_[i];
            if (entry.getObject() && (entry.flags & os::handleFlagCloseOnExec))
                handle = (static_cast<uint64_t>(entry.getGeneration()) << 32) |
                         (i + 1);
        }
        if (handle) close(handle);
    }
}

// Publish the closed-to-installation state before closing slots. Then free the
// backing array; concurrent attempts cannot repopulate a dying table.
void HandleTable::dispose() {
    {
        InterruptSpinLockGuard guard(lock_);
        disposed_ = true;
    }
    closeAll();
    InterruptSpinLockGuard guard(lock_);
    mm::freeHeap(entries_);
    entries_  = nullptr;
    capacity_ = 0;
}

// Extract one generation-tagged handle at a time, releasing the table lock
// before invoking close and any object-specific destructor path.
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

// User-space registry bootstrap handle number.
void Task::setBootstrapHandle(uint64_t handle) { bootstrapHandle_ = handle; }

// User-space registry bootstrap handle number.
uint64_t Task::bootstrapHandle() const { return bootstrapHandle_; }

// Standard stream handle number at index 0, 1 or 2.
void Task::setStandardHandle(uint32_t index, uint64_t handle) {
    if (index < 3) {
        standardHandles_[index] = handle;
    }
}

// Standard stream handle number, or zero for an invalid index.
uint64_t Task::standardHandle(uint32_t index) const {
    return index < 3 ? standardHandles_[index] : 0;
}

// Initial IPC kernel-service handle, separate from the user registry.
uint64_t Task::kernelHandle() const { return kernelHandle_; }

// Initial IPC kernel-service handle number.
void Task::setKernelHandle(uint64_t handle) { kernelHandle_ = handle; }

}  // namespace kernel::task
