/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/ipc/ipc.cc
 * Endpoint queues, authenticated requests, cancellation, and deadlines.
 */

#include "kernel/ipc.h"

#include <new>

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/sync.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace kernel::task {

namespace {
// Eight ordinary FIFO messages per endpoint. IRQ 0..15 use a separate pending
// bitmap so a full client queue cannot hide a device notification.
constexpr uint32_t queueCapacity = 8;
constexpr int waiting            = 1;
// Native negative error representation used by endpoint results.
constexpr int failure(os::ErrorCode error) { return static_cast<int>(error); }

// Stack-owned wait registration linked under an endpoint lock and, for finite
// deadlines, under timerLock. Complete atomically selects one result; all
// links must be removed before the owning stack can disappear.
struct Waiter {
    Thread* thread;
    Waiter* next      = nullptr;
    Waiter* timerNext = nullptr;
    uint64_t deadline;
    int result = waiting;
    bool timed = false;
};

// Caller-owned RPC state. The token identifies the request independently of
// server thread scheduling; serverTask authorizes replies, while serverThread
// tracks death of the thread that originally claimed it. replyTarget points to
// caller kernel storage, never directly to another task's user address.
struct PendingCall {
    Thread* caller;
    Task* callerTask;
    Task* serverTask     = nullptr;
    Thread* serverThread = nullptr;
    uint64_t token;
    IpcMessage* replyTarget;
    uint64_t deadline;
    PendingCall* next = nullptr;
    int result        = waiting;
};

// The queue owns captured references until actual receive installs them. call
// is null for one-way sends and points at the caller-owned RPC record
// otherwise; cancellation must remove that pointer before stack teardown.
struct QueuedMessage {
    IpcMessage message{};
    HandleTable::Transfer handles;
    PendingCall* call = nullptr;
};

// Explicit permissions for trusted kernel clients; ordinary callers prove
// access by holding appropriately restricted handle-table entries.
struct AccessGrant {
    Task* task;
    uint32_t rights;
    AccessGrant* next;
};

SpinLock timerLock;
Waiter* timers;
SpinLock endpointListLock;
EndpointImplementation* endpoints;
Atomic<uint64_t> nextEndpointId{1};
Atomic<uint64_t> nextReplyToken{1};

// Pin an active endpoint and chain cleanup to the current thread. Nesting can
// occur during pager RPCs; remember the previous endpoint and restore it on
// normal return. Forced thread cleanup releases the pin even when this C++
// scope is never unwound.
class Operation {
   public:
    explicit Operation(Endpoint* endpoint)
        : endpoint_(endpoint),
          thread_(currentThread()),
          previous_(thread_->getActiveEndpoint()) {
        endpoint_->retain();
        cleanup_.attach(endpoint_, [](void* object) {
            static_cast<Endpoint*>(object)->release();
        });
        thread_->setActiveIpcCall(true);
        thread_->setActiveEndpoint(endpoint_);
    }
    ~Operation() {
        thread_->setActiveEndpoint(previous_);
        thread_->setActiveIpcCall(previous_ != nullptr);
    }

   private:
    Endpoint* endpoint_;
    Thread* thread_;
    Endpoint* previous_;
    ThreadCleanup cleanup_;
};

// Only one wake, timeout, cancellation, or close may choose the result.
// Compare/exchange makes later contenders harmless; waking makes the owner
// runnable but does not guarantee it has returned from the wait yet.
void complete(Waiter& waiter, int result) {
    int expected = waiting;
    if (__atomic_compare_exchange_n(&waiter.result, &expected, result, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        wakeThread(waiter.thread);
}

// Link finite waits by absolute monotonic deadline while their stack record is
// alive. Infinite waits never enter the global timer list.
void armTimer(Waiter& waiter) {
    if (waiter.deadline == os::ipcInfiniteDeadline) return;
    InterruptSpinLockGuard guard(timerLock);
    Waiter** link = &timers;
    while (*link && (*link)->deadline <= waiter.deadline)
        link = &(*link)->timerNext;
    waiter.timerNext = *link;
    *link            = &waiter;
    waiter.timed     = true;
}

// Remove the timer registration before the stack-owned Waiter goes away. A
// timer already completed by expiry is safe to disarm again.
void disarmTimer(Waiter& waiter) {
    InterruptSpinLockGuard guard(timerLock);
    if (!waiter.timed) return;
    Waiter** link = &timers;
    while (*link && *link != &waiter) link = &(*link)->timerNext;
    if (*link) *link = waiter.timerNext;
    waiter.timed     = false;
    waiter.timerNext = nullptr;
}

// Endpoint lock protects this unlink; timer membership is separate and must
// also be disarmed by the enclosing operation.
void removeWaiter(Waiter*& head, Waiter& waiter) {
    Waiter** link = &head;
    while (*link && *link != &waiter) link = &(*link)->next;
    if (*link) *link = waiter.next;
    waiter.next = nullptr;
}

// Prepare the scheduler wake handshake before publishing the waiter, append
// FIFO, then arm its deadline. Caller holds the endpoint condition lock so no
// message/close can pass between checking the condition and registering.
void prepareWaiter(Waiter*& head, Waiter& waiter) {
    prepareCurrentWait();
    Waiter** tail = &head;
    while (*tail) tail = &(*tail)->next;
    *tail = &waiter;
    armTimer(waiter);
}

// Skip already completed registrations and select the oldest still waiting
// entry. Queue order is preserved, but CPU execution order is not promised.
void wakeOne(Waiter*& head) {
    while (head) {
        Waiter* waiter = head;
        head           = waiter->next;
        waiter->next   = nullptr;
        if (__atomic_load_n(&waiter->result, __ATOMIC_ACQUIRE) != waiting)
            continue;
        complete(*waiter, 0);
        return;
    }
}

// Bound the fixed wire payload and handle vector before any array access.
bool validateMessage(const IpcMessage* message) {
    return message && message->length <= os::ipcDataWordCount &&
           message->handleCount <= 4;
}

// Overwrite client-supplied identity with the current kernel task/thread.
// Service authorization must use this authenticated identity or caller object.
void stampSender(IpcMessage& message) {
    Task* task        = task::getCurrent();
    Thread* thread    = currentThread();
    message.senderPid = task ? task->id() : 0;
    message.senderTid = thread ? thread->getId() : 0;
}

// Zero requests no waiting; a finite deadline is absolute monotonic time.
// Callers check availability first where immediate completion is permitted.
int deadlineError(uint64_t deadline) {
    if (!deadline) return failure(os::ErrorCode::WouldBlock);
    return deadline != os::ipcInfiniteDeadline &&
                   time::getMonotonicTime() >= deadline
               ? failure(os::ErrorCode::Timeout)
               : 0;
}
}  // namespace

bool hasRightLocked(const Endpoint* endpoint, Task* task, uint32_t right);
void unlinkCall(EndpointImplementation& state, PendingCall& call);
void closeLocked(EndpointImplementation& state, os::ErrorCode error,
                 bool permanent = true);
void discardCallMessage(EndpointImplementation& state, PendingCall& call);
int enqueue(Endpoint* endpoint, IpcMessage& message, PendingCall* call,
            uint64_t deadline, bool kernelOrigin = false);

// State hidden from headers: one lock serializes queue slots, endpoint
// closure, access grants and pending RPC state. Separate sender/receiver/
// caller wait lists share the scheduler wake protocol.
class EndpointImplementation {
   public:
    // Endpoint state lock.
    SpinLock& getLock() { return lock; }

   private:
    friend class Endpoint;
    friend bool hasRightLocked(const Endpoint*, Task*, uint32_t);
    friend void unlinkCall(EndpointImplementation&, PendingCall&);
    friend void closeLocked(EndpointImplementation&, os::ErrorCode, bool);
    friend void discardCallMessage(EndpointImplementation&, PendingCall&);
    friend int enqueue(Endpoint*, IpcMessage&, PendingCall*, uint64_t, bool);
    SpinLock lock;
    Endpoint* endpoint;
    EndpointService* service = nullptr;
    EndpointImplementation* next;
    QueuedMessage queue[queueCapacity];
    uint32_t head        = 0;
    uint32_t count       = 0;
    Waiter* receivers    = nullptr;
    Waiter* senders      = nullptr;
    Waiter* callers      = nullptr;
    PendingCall* calls   = nullptr;
    AccessGrant* grants  = nullptr;
    bool destroyed       = false;
    uint16_t pendingIrqs = 0;
};

// Translate endpoint operations to handle rights and check actual object
// possession. Explicit grants and owner access are trusted internal paths. The
// endpoint state lock is held by the caller.
bool hasRightLocked(const Endpoint* endpoint, Task* task, uint32_t right) {
    if (!task) return false;
    const uint32_t rights =
        ((right & endpointRightSend) ? handleRightWrite : 0) |
        ((right & endpointRightReceive) ? handleRightRead : 0) |
        ((right & endpointRightManage) ? handleRightManage : 0);
    if (task->handles().hasObjectRights(const_cast<Endpoint*>(endpoint),
                                        HandleType::Endpoint, rights))
        return true;
    // Explicit grants are only for trusted in-kernel clients (including
    // probes).
    for (auto* grant = endpoint->getImplementation()->grants; grant;
         grant       = grant->next)
        if (grant->task == task && (grant->rights & right) == right)
            return true;
    return task == endpoint->getOwner();
}

// Remove the caller-owned record before its enclosing stack frame returns.
void unlinkCall(EndpointImplementation& state, PendingCall& call) {
    PendingCall** link = &state.calls;
    while (*link && *link != &call) link = &(*link)->next;
    if (*link) *link = call.next;
    call.next = nullptr;
}

// Finish every kind of waiter and release queued transfers before dropping
// receiver ownership. permanent=false keeps a recoverable endpoint reusable,
// but old requests still fail and are not replayed into the new instance.
void closeLocked(EndpointImplementation& state, os::ErrorCode error,
                 bool permanent) {
    if (state.destroyed) return;
    state.destroyed = permanent;
    Waiter* lists[] = {state.receivers, state.senders, state.callers};
    for (auto* list : lists)
        for (auto* waiter = list; waiter; waiter = waiter->next)
            complete(*waiter, failure(error));
    for (auto* call = state.calls; call; call = call->next)
        call->result = failure(error);
    for (uint32_t i = 0; i < state.count; ++i) {
        auto& queued = state.queue[(state.head + i) % queueCapacity];
        queued.handles.clear();
        queued = {};
    }
    state.count       = 0;
    state.pendingIrqs = 0;
}

// Remove an unclaimed request and compact the ring without reordering other
// messages. If receive already claimed it there is no queued slot to undo;
// invalidating the RPC cannot roll back server-side work already performed.
void discardCallMessage(EndpointImplementation& state, PendingCall& call) {
    for (uint32_t i = 0; i < state.count; ++i) {
        const uint32_t index = (state.head + i) % queueCapacity;
        if (state.queue[index].call != &call) continue;
        state.queue[index].handles.clear();
        for (uint32_t j = i; j + 1 < state.count; ++j)
            state.queue[(state.head + j) % queueCapacity] =
                state.queue[(state.head + j + 1) % queueCapacity];
        auto& last =
            state.queue[(state.head + state.count - 1) % queueCapacity];
        last = {};
        --state.count;
        wakeOne(state.senders);
        return;
    }
}

// Check endpoint/ownership/rights under the state lock, then either commit the
// message and its captured references or register for space. After capture
// succeeds, no fallible step may precede publication because move handles have
// already left the sender. kernelOrigin is an internal access bypass, not a
// user-message flag.
int enqueue(Endpoint* endpoint, IpcMessage& message, PendingCall* call,
            uint64_t deadline, bool kernelOrigin) {
    auto& state  = *endpoint->getImplementation();
    Task* sender = task::getCurrent();
    while (true) {
        Waiter waiter{currentThread(), nullptr, nullptr, deadline};
        {
            InterruptSpinLockGuard guard(state.lock);
            if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
            if (!endpoint->getOwner()) return failure(os::ErrorCode::PeerDied);
            if (!kernelOrigin &&
                !hasRightLocked(endpoint, sender, endpointRightSend))
                return failure(os::ErrorCode::PermissionDenied);
            if (state.count < queueCapacity) {
                auto& queued =
                    state.queue[(state.head + state.count) % queueCapacity];
                if (!queued.handles.capture(sender->handles(), message))
                    return failure(os::ErrorCode::InvalidHandle);
                queued.message = message;
                queued.call    = call;
                ++state.count;
                if (call) {
                    call->next  = state.calls;
                    state.calls = call;
                }
                wakeOne(state.receivers);
                return 0;
            }
            const int error = deadlineError(deadline);
            if (error) return error;
            prepareWaiter(state.senders, waiter);
        }
        blockWaitingThread();
        {
            InterruptSpinLockGuard guard(state.lock);
            removeWaiter(state.senders, waiter);
            disarmTimer(waiter);
        }
        const int result = __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
        if (result < 0) return result;
    }
}
// Initialize global endpoint/timer tracking before any endpoint is used.
void Endpoint::init() {
    timerLock.init();
    endpointListLock.init();
    device::serialWrite("[IPC] endpoint IPC initialized\n");
}

// Object lifetime reference; active operations and installed handles hold it.
void Endpoint::retain() {
    __atomic_add_fetch(&references, 1U, __ATOMIC_RELAXED);
}
// Drop a reference without freeing inline. Timer maintenance later reaps
// zero-reference endpoints, avoiding destruction while their locks are held.
void Endpoint::release() {
    __atomic_sub_fetch(&references, 1U, __ATOMIC_RELEASE);
}
// Permanent endpoint closure state under its state lock.
bool Endpoint::isClosed() {
    InterruptSpinLockGuard guard(implementation->getLock());
    return implementation->destroyed;
}
// Close when the actual owner loses its receiving capability. A handle held by
// some unrelated task must not revoke the owner's endpoint.
void Endpoint::receiverClosed(Task* task) {
    InterruptSpinLockGuard guard(implementation->getLock());
    if (getOwner() == task)
        closeLocked(*implementation, os::ErrorCode::EndpointClosed);
}

// Allocate endpoint plus private state before linking into the global list.
// The creation reference belongs to owner until destroy/owner exit; installed
// handles acquire additional references. service selects resident dispatch
// instead of ordinary user-server queueing.
Endpoint* Endpoint::create(EndpointService* service) {
    Task* owner = task::getCurrent();
    if (!owner) return nullptr;
    void* storage        = mm::allocateHeap(sizeof(Endpoint));
    void* implementation = mm::allocateHeap(sizeof(EndpointImplementation));
    if (!storage || !implementation) {
        mm::freeHeap(storage);
        mm::freeHeap(implementation);
        return nullptr;
    }
    auto* endpoint = new (storage) Endpoint;
    auto* state    = new (implementation) EndpointImplementation{};
    state->lock.init();
    state->endpoint = endpoint;
    state->service  = service;
    endpoint->setId(nextEndpointId.fetchAdd(1, MemoryOrder::Relaxed));
    endpoint->setOwner(owner);
    endpoint->setImplementation(state);
    InterruptSpinLockGuard guard(endpointListLock);
    state->next = endpoints;
    endpoints   = state;
    return endpoint;
}

// Require manage access, complete pending work, then release the owner
// reference. Handle/operation references can keep the closed endpoint alive
// until deferred reaping; closure is not immediate memory destruction.
void Endpoint::destroy() {
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    if (state.destroyed ||
        !hasRightLocked(this, task::getCurrent(), endpointRightManage))
        return;
    closeLocked(state, os::ErrorCode::EndpointClosed);
    if (getOwner()) {
        setOwner(nullptr);
        release();
    }
}
// Only the task that claimed a token can inspect its liveness. Delayed servers
// use this before retaining more work for an abandoned RPC.
bool Endpoint::requestActive(uint64_t token) {
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    for (auto* call = state.calls; call; call = call->next)
        if (call->token == token && call->serverTask == task::getCurrent())
            return call->result == waiting && !deadlineError(call->deadline);
    return false;
}

// During task teardown, clear owned receivers and explicit grants under
// list-then-state lock ordering. Recovery-protected endpoints retain their
// identity but not old requests; other endpoints close permanently.
void Endpoint::closeOwner(Task* owner) {
    InterruptSpinLockGuard listGuard(endpointListLock);
    for (auto* state = endpoints; state; state = state->next) {
        InterruptSpinLockGuard guard(state->lock);
        if (state->endpoint->getOwner() == owner) {
            closeLocked(*state, os::ErrorCode::PeerDied,
                        state->endpoint->recoveryRole == 0);
            state->endpoint->setOwner(nullptr);
            state->endpoint->release();
        }
        AccessGrant** link = &state->grants;
        while (*link) {
            auto* grant = *link;
            if (grant->task != owner) {
                link = &grant->next;
                continue;
            }
            *link = grant->next;
            mm::freeHeap(grant);
        }
    }
}

// Protect only a live user-server endpoint owned by this managed role. Kernel
// service endpoints cannot be turned into recoverable user receivers.
bool Endpoint::protectRecovery(Task& task) {
    InterruptSpinLockGuard guard(implementation->getLock());
    const auto role = static_cast<uint32_t>(task.serviceRole());
    if (implementation->destroyed || implementation->service ||
        getOwner() != &task || !task.managedService() || !role ||
        (recoveryRole && recoveryRole != role))
        return false;
    recoveryRole = role;
    return true;
}
// A replacement of the same role may take an ownerless protected endpoint.
// Reacquire the owner reference exactly once; another live owner is rejected.
bool Endpoint::adoptRecovery(Task& task) {
    InterruptSpinLockGuard guard(implementation->getLock());
    if (implementation->destroyed || !recoveryRole ||
        recoveryRole != static_cast<uint32_t>(task.serviceRole()) ||
        (getOwner() && getOwner() != &task))
        return false;
    if (!getOwner()) {
        retain();
        setOwner(&task);
    }
    return true;
}
// Remove only matching-role protection; object lifetime still follows its
// owner, handle and operation references.
void Endpoint::releaseRecovery(Task& task) {
    InterruptSpinLockGuard guard(implementation->getLock());
    if (recoveryRole == static_cast<uint32_t>(task.serviceRole()))
        recoveryRole = 0;
}

// Install a trusted internal grant after manage authorization. Validate the
// whole rights mask; user clients normally receive restricted handles instead.
bool Endpoint::grantAccess(Task* task, uint32_t rights) {
    if (!task || (rights & ~(endpointRightSend | endpointRightReceive |
                             endpointRightManage)))
        return false;
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    if (state.destroyed ||
        !hasRightLocked(this, task::getCurrent(), endpointRightManage))
        return false;
    auto* grant =
        static_cast<AccessGrant*>(mm::allocateHeap(sizeof(AccessGrant)));
    if (!grant) return false;
    *grant       = {task, rights, state.grants};
    state.grants = grant;
    return true;
}

// Fork/bootstrap helper that preserves optional handle values while sharing
// the underlying endpoint Reference through the two handle tables.
uint64_t Endpoint::duplicateHandle(Task* source, Task* destination,
                                   uint64_t handle,
                                   uint64_t destinationHandle) {
    if (!source || !destination) return 0;
    // The fork/bootstrap path preserves handle values when requested.
    return source->handles().copyTo(destination->handles(), handle,
                                    destinationHandle);
}

// Queue a one-way message with authenticated sender and no reply token.
// Resident kernel services require call rather than queue-based send.
int Endpoint::send(IpcMessage* message, uint64_t deadline) {
    Operation operation(this);
    if (getImplementation()->service)
        return failure(os::ErrorCode::NotSupported);
    if (!validateMessage(message) || !getImplementation())
        return failure(os::ErrorCode::InvalidArgument);
    IpcMessage copy = *message;
    copy.replyToken = 0;
    stampSender(copy);
    return enqueue(this, copy, nullptr, deadline);
}

// Hold receive rights, prefer a coalesced IRQ, then claim the oldest queued
// message. Install all captured handles into the task actually receiving; only
// after successful installation remove the slot and wake one sender.
int Endpoint::receive(IpcMessage* message, uint64_t deadline) {
    Operation operation(this);
    if (getImplementation()->service)
        return failure(os::ErrorCode::NotSupported);
    if (!message || !getImplementation())
        return failure(os::ErrorCode::InvalidArgument);
    auto& state = *getImplementation();
    while (true) {
        Waiter waiter{currentThread(), nullptr, nullptr, deadline};
        {
            InterruptSpinLockGuard guard(state.lock);
            if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
            if (!hasRightLocked(this, task::getCurrent(), endpointRightReceive))
                return failure(os::ErrorCode::PermissionDenied);
            if (recoveryRole && getOwner() != task::getCurrent())
                return failure(os::ErrorCode::PermissionDenied);
            if (state.pendingIrqs) {
                const uint32_t irq = __builtin_ctz(state.pendingIrqs);
                state.pendingIrqs &= static_cast<uint16_t>(~(1U << irq));
                *message         = {};
                message->type    = 0x495251;
                message->length  = 1;
                message->data[0] = irq;
                return 0;
            }
            if (state.count) {
                auto& queued = state.queue[state.head];
                // Destination allocation can fail without consuming the queued
                // message. No partial handle vector is exposed; the receiver
                // can retry later.
                if (!queued.handles.install(task::getCurrent()->handles(),
                                            queued.message))
                    return failure(os::ErrorCode::OutOfMemory);
                *message = queued.message;
                if (queued.call) {
                    queued.call->serverTask   = task::getCurrent();
                    queued.call->serverThread = currentThread();
                }
                queued     = {};
                state.head = (state.head + 1) % queueCapacity;
                --state.count;
                wakeOne(state.senders);
                return 0;
            }
            const int error = deadlineError(deadline);
            if (error) return error;
            prepareWaiter(state.receivers, waiter);
        }
        blockWaitingThread();
        {
            InterruptSpinLockGuard guard(state.lock);
            removeWaiter(state.receivers, waiter);
            disarmTimer(waiter);
        }
        const int result = __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
        if (result < 0) return result;
    }
}

// Match the explicit token and the authenticated claiming process, not the
// current server thread's last request. Capture reply moves provisionally,
// then atomically install at the caller before writing its replyTarget and
// waking it. Invalid/expired tokens have no right to access that storage.
int Endpoint::reply(IpcMessage* message) {
    Operation operation(this);
    if (!validateMessage(message) || !message->replyToken ||
        !getImplementation())
        return failure(os::ErrorCode::InvalidArgument);
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
    PendingCall* call = state.calls;
    while (call && call->token != message->replyToken) call = call->next;
    if (!call || call->serverTask != task::getCurrent() ||
        call->result != waiting || deadlineError(call->deadline) ||
        !hasRightLocked(this, task::getCurrent(), endpointRightReceive))
        return failure(os::ErrorCode::InvalidArgument);
    IpcMessage reply = *message;
    stampSender(reply);
    HandleTable::Transfer transfer;
    if (!transfer.capture(task::getCurrent()->handles(), reply, false))
        return failure(os::ErrorCode::InvalidHandle);
    if (!transfer.install(call->callerTask->handles(), reply)) {
        transfer.clear();
        return failure(os::ErrorCode::OutOfMemory);
    }
    *call->replyTarget = reply;
    call->result       = 0;
    for (auto* waiter = state.callers; waiter; waiter = waiter->next)
        if (waiter->thread == call->caller) complete(*waiter, 0);
    return 0;
}

// Only taskKernelService has authority to change the current syscall frame. A
// user endpoint with the same numeric operation cannot select this path.
bool Endpoint::handlesContext(uint64_t operation) const {
    return implementation && implementation->service == &taskKernelService() &&
           ((operation >= static_cast<uint64_t>(os::TaskOperation::ExitTask) &&
             operation <=
                 static_cast<uint64_t>(os::TaskOperation::StandardHandle)) ||
            operation ==
                static_cast<uint64_t>(os::TaskOperation::SignalReturn));
}
// Execute caller-context mechanisms inline after access/deadline checks,
// without publishing a stack-backed RPC waiter. These operations can exit,
// replace the address space, or restore signal context; returning through a
// normal pending-call record would refer to obsolete execution state.
int Endpoint::callImmediate(IpcMessage& request, IpcMessage& reply,
                            uint64_t deadline, bool kernelOrigin) {
    Operation operation(this);
    if (!validateMessage(&request) || request.handleCount ||
        !handlesContext(request.type))
        return failure(os::ErrorCode::InvalidArgument);
    auto& state = *getImplementation();
    {
        InterruptSpinLockGuard guard(state.lock);
        if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
        if (!kernelOrigin &&
            !hasRightLocked(this, task::getCurrent(), endpointRightSend))
            return failure(os::ErrorCode::PermissionDenied);
        if (deadline && deadlineError(deadline)) return deadlineError(deadline);
    }
    stampSender(request);
    reply = {};
    return state.service->call(*task::getCurrent(), request, reply);
}
// Untrusted/user entry: enforce endpoint rights before native or queued call.
int Endpoint::call(IpcMessage* request, IpcMessage* reply, uint64_t deadline) {
    return callImpl(request, reply, deadline, false);
}
// Internal caller entry; bypass endpoint possession only, while the concrete
// service continues to receive the real current Task for its own checks.
int Endpoint::callFromKernel(IpcMessage* request, IpcMessage* reply,
                             uint64_t deadline) {
    return callImpl(request, reply, deadline, true);
}
// Resident services execute inline, with a waiter registered when ordinary
// cancellation/deadline accounting is needed. User services enqueue a unique
// PendingCall, then wait for a token-authorized reply. In both cases active
// operation ownership prevents endpoint/stack lifetime from ending early.
int Endpoint::callImpl(IpcMessage* request, IpcMessage* reply,
                       uint64_t deadline, bool kernelOrigin) {
    Operation operation(this);
    if (!validateMessage(request) || !reply || !getImplementation())
        return failure(os::ErrorCode::InvalidArgument);
    if (auto* service = getImplementation()->service) {
        if (request->handleCount)
            return failure(os::ErrorCode::InvalidArgument);
        auto& state = *getImplementation();
        if (handlesContext(request->type))
            return callImmediate(*request, *reply, deadline, kernelOrigin);
        Waiter waiter{currentThread(), nullptr, nullptr,
                      deadline ? deadline : os::ipcInfiniteDeadline};
        IpcMessage copy = *request;
        stampSender(copy);
        copy.replyToken = nextReplyToken.fetchAdd(1, MemoryOrder::Relaxed);
        {
            InterruptSpinLockGuard guard(state.lock);
            if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
            if (!kernelOrigin &&
                !hasRightLocked(this, task::getCurrent(), endpointRightSend))
                return failure(os::ErrorCode::PermissionDenied);
            if (deadline && deadlineError(deadline))
                return deadlineError(deadline);
            prepareWaiter(state.callers, waiter);
            waiter.thread->setIpcWaitResult(&waiter.result, deadline == 0);
        }
        *reply     = {};
        int result = service->call(*task::getCurrent(), copy, *reply);
        {
            InterruptSpinLockGuard guard(state.lock);
            complete(waiter, 0);
            removeWaiter(state.callers, waiter);
            disarmTimer(waiter);
            waiter.thread->setIpcWaitResult(nullptr);
            const int aborted =
                __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
            // A successful native service call may already have committed a
            // non-repeatable operation (for example reaping a child). Do not
            // discard its reply when a signal races with that completion.
            if (aborted < 0 && result != 0) result = aborted;
        }
        reply->replyToken = copy.replyToken;
        if (result) {
            for (uint32_t i = 0; i < reply->handleCount; ++i)
                task::getCurrent()->handles().close(reply->handles[i]);
            reply->handleCount = 0;
        }
        return result;
    }
    // The record lives until completion/cancellation removes it from
    // state.calls. Its replyTarget remains kernel-local even while the server
    // executes under a different CR3, which is why syscall code copies to user
    // memory afterward.
    PendingCall call{currentThread(),
                     task::getCurrent(),
                     nullptr,
                     nullptr,
                     nextReplyToken.fetchAdd(1, MemoryOrder::Relaxed),
                     reply,
                     deadline};
    IpcMessage copy = *request;
    copy.replyToken = call.token;
    stampSender(copy);
    int result = enqueue(this, copy, &call, deadline, kernelOrigin);
    if (result) return result;
    auto& state = *getImplementation();
    while (true) {
        Waiter waiter{call.caller, nullptr, nullptr, deadline};
        {
            InterruptSpinLockGuard guard(state.lock);
            if (call.result != waiting) {
                result = call.result;
                break;
            }
            result = deadlineError(deadline);
            if (result) {
                discardCallMessage(state, call);
                break;
            }
            prepareWaiter(state.callers, waiter);
        }
        blockWaitingThread();
        {
            InterruptSpinLockGuard guard(state.lock);
            removeWaiter(state.callers, waiter);
            disarmTimer(waiter);
            if (call.result != waiting)
                result = call.result;
            else
                result = __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
            if (result < 0) {
                discardCallMessage(state, call);
                break;
            }
        }
    }
    {
        InterruptSpinLockGuard guard(state.lock);
        unlinkCall(state, call);
    }
    return result;
}

// Allow a task to cancel only its own thread's registrations on this endpoint.
// Unclaimed RPC messages are removed; claimed calls lose reply authority,
// without reversing server-side effects already committed.
int Endpoint::cancel(uint64_t threadId) {
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    int result = failure(os::ErrorCode::NotFound);
    for (auto* call = state.calls; call; call = call->next) {
        if (call->caller->getId() == threadId &&
            call->callerTask == task::getCurrent() && call->result == waiting) {
            call->result = failure(os::ErrorCode::Cancelled);
            discardCallMessage(state, *call);
            result = 0;
        }
    }
    Waiter* lists[] = {state.senders, state.receivers, state.callers};
    for (Waiter* list : lists) {
        for (auto* waiter = list; waiter; waiter = waiter->next) {
            if (waiter->thread->getId() != threadId ||
                waiter->thread->getTask() != task::getCurrent())
                continue;
            complete(*waiter, failure(os::ErrorCode::Cancelled));
            result = 0;
        }
    }
    return result;
}

// Complete expired stack waiters while holding timerLock. Reap endpoints only
// after reference count reaches zero under global-list serialization; release
// never frees a locked endpoint inline.
void Endpoint::expireWaits(uint64_t now) {
    {
        InterruptSpinLockGuard guard(timerLock);
        while (timers && timers->deadline <= now) {
            Waiter* waiter    = timers;
            timers            = waiter->timerNext;
            waiter->timerNext = nullptr;
            waiter->timed     = false;
            complete(*waiter, failure(os::ErrorCode::Timeout));
        }
    }
    InterruptSpinLockGuard guard(endpointListLock);
    EndpointImplementation** link = &endpoints;
    while (*link) {
        auto* state        = *link;
        Endpoint* endpoint = state->endpoint;
        if (__atomic_load_n(&endpoint->references, __ATOMIC_ACQUIRE) != 0) {
            link = &state->next;
            continue;
        }
        *link = state->next;
        while (state->grants) {
            auto* grant   = state->grants;
            state->grants = grant->next;
            mm::freeHeap(grant);
        }
        mm::freeHeap(state);
        mm::freeHeap(endpoint);
    }
}

// Teardown path: remove endpoint/timer links and caller-owned RPC records
// before stack freeing. A dead claiming thread fails its still-pending RPCs
// with PeerDied. The target must be current-and-exiting or already quiesced;
// ThreadCleanup later releases nested active-operation references.
void Endpoint::cancelThread(Thread* thread) {
    {
        InterruptSpinLockGuard listGuard(endpointListLock);
        for (auto* state = endpoints; state; state = state->next) {
            InterruptSpinLockGuard guard(state->lock);
            Waiter** heads[] = {&state->senders, &state->receivers,
                                &state->callers};
            for (Waiter** head : heads) {
                Waiter** link = head;
                while (*link) {
                    Waiter* waiter = *link;
                    if (waiter->thread != thread) {
                        link = &waiter->next;
                        continue;
                    }
                    *link = waiter->next;
                    disarmTimer(*waiter);
                    complete(*waiter, failure(os::ErrorCode::Interrupted));
                }
            }
            PendingCall** link = &state->calls;
            while (*link) {
                PendingCall* call = *link;
                if (call->serverThread == thread && call->result == waiting) {
                    call->result       = failure(os::ErrorCode::PeerDied);
                    call->serverThread = nullptr;
                    for (auto* waiter = state->callers; waiter;
                         waiter       = waiter->next)
                        if (waiter->thread == call->caller)
                            complete(*waiter, failure(os::ErrorCode::PeerDied));
                }
                if (call->caller != thread) {
                    link = &call->next;
                    continue;
                }
                discardCallMessage(*state, *call);
                *link = call->next;
            }
        }
    }
    // Operation references, including nested pager calls, belong to
    // ThreadCleanup.
    thread->setActiveEndpoint(nullptr);
    thread->setActiveIpcCall(false);
    thread->setIpcWaitResult(nullptr);
}

// Signal interruption completes waits but leaves normal operation return paths
// to remove their registrations. Unlike forced teardown this does not free the
// target stack or transfer its temporary ownership.
void Endpoint::interruptThread(Thread* thread) {
    InterruptSpinLockGuard listGuard(endpointListLock);
    for (auto* state = endpoints; state; state = state->next) {
        InterruptSpinLockGuard guard(state->lock);
        for (auto* call = state->calls; call; call = call->next) {
            if (call->caller != thread || call->result != waiting) continue;
            call->result = failure(os::ErrorCode::Interrupted);
            discardCallMessage(*state, *call);
        }
        Waiter* lists[] = {state->senders, state->receivers, state->callers};
        for (auto* list : lists)
            for (auto* waiter = list; waiter; waiter = waiter->next)
                if (waiter->thread == thread)
                    complete(*waiter, failure(os::ErrorCode::Interrupted));
    }
}

// IRQ notifications set a bitmap bit and wake a receiver without waiting for a
// free ordinary slot. Repeated same-IRQ events coalesce, so the payload is a
// pending source, not an interrupt count. Other types may fail if full.
bool Endpoint::notifyFromInterrupt(uint64_t type, uint64_t value) {
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    if (state.destroyed) return false;
    if (type == 0x495251 && value < 16) {
        state.pendingIrqs |= static_cast<uint16_t>(1U << value);
        wakeOne(state.receivers);
        return true;
    }
    if (state.count == queueCapacity) return false;
    auto& queued   = state.queue[(state.head + state.count) % queueCapacity];
    queued.message = {};
    queued.message.type    = type;
    queued.message.length  = 1;
    queued.message.data[0] = value;
    ++state.count;
    wakeOne(state.receivers);
    return true;
}

}  // namespace kernel::task
