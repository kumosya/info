#include "kernel/ipc.h"

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/new.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "kernel/time.h"
#include "kernel/core_service.h"

namespace kernel {
namespace {
constexpr uint32_t queueCapacity = 8;
constexpr int waiting = 1;
constexpr int failure(os::ErrorCode error) { return static_cast<int>(error); }

struct Waiter {
    Thread* thread;
    Waiter* next = nullptr;
    Waiter* timerNext = nullptr;
    uint64_t deadline;
    int result = waiting;
    bool timed = false;
};

struct PendingCall {
    Thread* caller;
    Task* callerTask;
    Task* serverTask = nullptr;
    Thread* serverThread = nullptr;
    uint64_t token;
    IpcMessage* replyTarget;
    uint64_t deadline;
    PendingCall* next = nullptr;
    int result = waiting;
};

struct QueuedMessage {
    IpcMessage message{};
    HandleTable::Transfer handles;
    PendingCall* call = nullptr;
};

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

class Operation {
   public:
    explicit Operation(Endpoint* endpoint) : endpoint_(endpoint), thread_(Thread::getCurrent()) {
        endpoint_->retain();
        thread_->setActiveIpcCall(true);
        thread_->setActiveEndpoint(endpoint_);
    }
    ~Operation() {
        thread_->setActiveEndpoint(nullptr);
        thread_->setActiveIpcCall(false);
        endpoint_->release();
    }
   private:
    Endpoint* endpoint_;
    Thread* thread_;
};

void complete(Waiter& waiter, int result) {
    int expected = waiting;
    if (__atomic_compare_exchange_n(&waiter.result, &expected, result, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        Thread::wake(waiter.thread);
}

void armTimer(Waiter& waiter) {
    if (waiter.deadline == os::ipcInfiniteDeadline) return;
    InterruptSpinLockGuard guard(timerLock);
    Waiter** link = &timers;
    while (*link && (*link)->deadline <= waiter.deadline) link = &(*link)->timerNext;
    waiter.timerNext = *link;
    *link = &waiter;
    waiter.timed = true;
}

void disarmTimer(Waiter& waiter) {
    InterruptSpinLockGuard guard(timerLock);
    if (!waiter.timed) return;
    Waiter** link = &timers;
    while (*link && *link != &waiter) link = &(*link)->timerNext;
    if (*link) *link = waiter.timerNext;
    waiter.timed = false;
    waiter.timerNext = nullptr;
}

void removeWaiter(Waiter*& head, Waiter& waiter) {
    Waiter** link = &head;
    while (*link && *link != &waiter) link = &(*link)->next;
    if (*link) *link = waiter.next;
    waiter.next = nullptr;
}

void prepareWaiter(Waiter*& head, Waiter& waiter) {
    Thread::prepareWait();
    Waiter** tail = &head;
    while (*tail) tail = &(*tail)->next;
    *tail = &waiter;
    armTimer(waiter);
}

void wakeOne(Waiter*& head) {
    while (head) {
        Waiter* waiter = head;
        head = waiter->next;
        waiter->next = nullptr;
        if (__atomic_load_n(&waiter->result, __ATOMIC_ACQUIRE) != waiting) continue;
        complete(*waiter, 0);
        return;
    }
}

bool validateMessage(const IpcMessage* message) {
    return message && message->length <= os::ipcDataWordCount && message->handleCount <= 4;
}

void stampSender(IpcMessage& message) {
    Task* task = task::getCurrent();
    Thread* thread = Thread::getCurrent();
    message.senderPid = task ? task->id() : 0;
    message.senderTid = thread ? thread->getId() : 0;
}

int deadlineError(uint64_t deadline) {
    if (!deadline) return failure(os::ErrorCode::WouldBlock);
    return deadline != os::ipcInfiniteDeadline && time::getMonotonicTime() >= deadline
        ? failure(os::ErrorCode::Timeout) : 0;
}
}  // namespace

struct EndpointImplementation {
    SpinLock lock;
    Endpoint* endpoint;
    EndpointService* service = nullptr;
    EndpointImplementation* next;
    QueuedMessage queue[queueCapacity];
    uint32_t head = 0;
    uint32_t count = 0;
    Waiter* receivers = nullptr;
    Waiter* senders = nullptr;
    Waiter* callers = nullptr;
    PendingCall* calls = nullptr;
    AccessGrant* grants = nullptr;
    bool destroyed = false;
    uint16_t pendingIrqs = 0;
};

namespace {
bool hasRightLocked(const Endpoint* endpoint, Task* task, uint32_t right) {
    if (!task) return false;
    const uint32_t rights = ((right & endpointRightSend) ? handleRightWrite : 0) |
        ((right & endpointRightReceive) ? handleRightRead : 0) |
        ((right & endpointRightManage) ? handleRightManage : 0);
    if (task->handles().hasObjectRights(const_cast<Endpoint*>(endpoint), HandleType::Endpoint, rights))
        return true;
    // Explicit grants are only for trusted in-kernel clients (including probes).
    for (auto* grant = endpoint->getImplementation()->grants; grant; grant = grant->next)
        if (grant->task == task && (grant->rights & right) == right) return true;
    return task == endpoint->getOwner();
}

void unlinkCall(EndpointImplementation& state, PendingCall& call) {
    PendingCall** link = &state.calls;
    while (*link && *link != &call) link = &(*link)->next;
    if (*link) *link = call.next;
    call.next = nullptr;
}

void closeLocked(EndpointImplementation& state, os::ErrorCode error) {
    if (state.destroyed) return;
    state.destroyed = true;
    Waiter* lists[] = {state.receivers, state.senders, state.callers};
    for (auto* list : lists)
        for (auto* waiter = list; waiter; waiter = waiter->next) complete(*waiter, failure(error));
    for (auto* call = state.calls; call; call = call->next) call->result = failure(error);
    for (uint32_t i = 0; i < state.count; ++i) {
        auto& queued = state.queue[(state.head + i) % queueCapacity];
        queued.handles.clear();
        queued = {};
    }
    state.count = 0;
}

void discardCallMessage(EndpointImplementation& state, PendingCall& call) {
    for (uint32_t i = 0; i < state.count; ++i) {
        const uint32_t index = (state.head + i) % queueCapacity;
        if (state.queue[index].call != &call) continue;
        state.queue[index].handles.clear();
        for (uint32_t j = i; j + 1 < state.count; ++j)
            state.queue[(state.head + j) % queueCapacity] =
                state.queue[(state.head + j + 1) % queueCapacity];
        auto& last = state.queue[(state.head + state.count - 1) % queueCapacity];
        last = {};
        --state.count;
        wakeOne(state.senders);
        return;
    }
}

int enqueue(Endpoint* endpoint, IpcMessage& message, PendingCall* call, uint64_t deadline) {
    auto& state = *endpoint->getImplementation();
    Task* sender = task::getCurrent();
    while (true) {
        Waiter waiter{Thread::getCurrent(), nullptr, nullptr, deadline};
        {
            InterruptSpinLockGuard guard(state.lock);
            if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
            if (!hasRightLocked(endpoint, sender, endpointRightSend))
                return failure(os::ErrorCode::PermissionDenied);
            if (state.count < queueCapacity) {
                auto& queued = state.queue[(state.head + state.count) % queueCapacity];
                if (!queued.handles.capture(sender->handles(), message))
                    return failure(os::ErrorCode::InvalidHandle);
                queued.message = message;
                queued.call = call;
                ++state.count;
                if (call) { call->next = state.calls; state.calls = call; }
                wakeOne(state.receivers);
                return 0;
            }
            const int error = deadlineError(deadline);
            if (error) return error;
            prepareWaiter(state.senders, waiter);
        }
        Thread::blockCurrent();
        {
            InterruptSpinLockGuard guard(state.lock);
            removeWaiter(state.senders, waiter);
            disarmTimer(waiter);
        }
        const int result = __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
        if (result < 0) return result;
    }
}
}  // namespace

void Endpoint::init() {
    timerLock.init();
    endpointListLock.init();
    serial::serialWrite("[IPC] endpoint IPC initialized\n");
}

void Endpoint::retain() { __atomic_add_fetch(&references, 1U, __ATOMIC_RELAXED); }
void Endpoint::release() { __atomic_sub_fetch(&references, 1U, __ATOMIC_RELEASE); }
bool Endpoint::isClosed() {
    InterruptSpinLockGuard guard(implementation->lock);
    return implementation->destroyed;
}
void Endpoint::receiverClosed(Task* task) {
    InterruptSpinLockGuard guard(implementation->lock);
    if (getOwner() == task) closeLocked(*implementation, os::ErrorCode::EndpointClosed);
}

Endpoint* Endpoint::create(EndpointService* service) {
    Task* owner = task::getCurrent();
    if (!owner) return nullptr;
    void* storage = heap::kernelAllocate(sizeof(Endpoint));
    void* implementation = heap::kernelAllocate(sizeof(EndpointImplementation));
    if (!storage || !implementation) {
        heap::kernelFree(storage);
        heap::kernelFree(implementation);
        return nullptr;
    }
    auto* endpoint = new (storage) Endpoint;
    auto* state = new (implementation) EndpointImplementation{};
    state->lock.init();
    state->endpoint = endpoint;
    state->service = service;
    endpoint->setId(nextEndpointId.fetchAdd(1, MemoryOrder::Relaxed));
    endpoint->setOwner(owner);
    endpoint->setImplementation(state);
    InterruptSpinLockGuard guard(endpointListLock);
    state->next = endpoints;
    endpoints = state;
    return endpoint;
}

void Endpoint::destroy() {
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    if (state.destroyed || !hasRightLocked(this, task::getCurrent(), endpointRightManage)) return;
    closeLocked(state, os::ErrorCode::EndpointClosed);
}

void Endpoint::closeOwner(Task* owner) {
    InterruptSpinLockGuard listGuard(endpointListLock);
    for (auto* state = endpoints; state; state = state->next) {
        InterruptSpinLockGuard guard(state->lock);
        if (state->endpoint->getOwner() == owner) {
            closeLocked(*state, os::ErrorCode::PeerDied);
            state->endpoint->setOwner(nullptr);
            state->endpoint->release();
        }
        AccessGrant** link = &state->grants;
        while (*link) {
            auto* grant = *link;
            if (grant->task != owner) { link = &grant->next; continue; }
            *link = grant->next;
            heap::kernelFree(grant);
        }
    }
}

bool Endpoint::grantAccess(Task* task, uint32_t rights) {
    if (!task || (rights & ~(endpointRightSend | endpointRightReceive | endpointRightManage))) return false;
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    if (state.destroyed || !hasRightLocked(this, task::getCurrent(), endpointRightManage)) return false;
    auto* grant = static_cast<AccessGrant*>(heap::kernelAllocate(sizeof(AccessGrant)));
    if (!grant) return false;
    *grant = {task, rights, state.grants};
    state.grants = grant;
    return true;
}

uint64_t Endpoint::duplicateHandle(Task* source, Task* destination, uint64_t handle,
                                   uint64_t destinationHandle) {
    if (!source || !destination) return 0;
    // The fork/bootstrap path preserves handle values when requested.
    return source->handles().copyTo(destination->handles(), handle, destinationHandle);
}

int Endpoint::send(IpcMessage* message, uint64_t deadline) {
    Operation operation(this);
    if (getImplementation()->service) return failure(os::ErrorCode::NotSupported);
    if (!validateMessage(message) || !getImplementation()) return failure(os::ErrorCode::InvalidArgument);
    IpcMessage copy = *message;
    copy.replyToken = 0;
    stampSender(copy);
    return enqueue(this, copy, nullptr, deadline);
}

int Endpoint::receive(IpcMessage* message, uint64_t deadline) {
    Operation operation(this);
    if (getImplementation()->service) return failure(os::ErrorCode::NotSupported);
    if (!message || !getImplementation()) return failure(os::ErrorCode::InvalidArgument);
    auto& state = *getImplementation();
    while (true) {
        Waiter waiter{Thread::getCurrent(), nullptr, nullptr, deadline};
        {
            InterruptSpinLockGuard guard(state.lock);
            if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
            if (!hasRightLocked(this, task::getCurrent(), endpointRightReceive))
                return failure(os::ErrorCode::PermissionDenied);
            if (state.pendingIrqs) {
                const uint32_t irq = __builtin_ctz(state.pendingIrqs);
                state.pendingIrqs &= static_cast<uint16_t>(~(1U << irq));
                *message = {};
                message->type = 0x495251;
                message->length = 1;
                message->data[0] = irq;
                return 0;
            }
            if (state.count) {
                auto& queued = state.queue[state.head];
                if (!queued.handles.install(task::getCurrent()->handles(), queued.message))
                    return failure(os::ErrorCode::OutOfMemory);
                *message = queued.message;
                if (queued.call) {
                    queued.call->serverTask = task::getCurrent();
                    queued.call->serverThread = Thread::getCurrent();
                }
                queued = {};
                state.head = (state.head + 1) % queueCapacity;
                --state.count;
                wakeOne(state.senders);
                return 0;
            }
            const int error = deadlineError(deadline);
            if (error) return error;
            prepareWaiter(state.receivers, waiter);
        }
        Thread::blockCurrent();
        {
            InterruptSpinLockGuard guard(state.lock);
            removeWaiter(state.receivers, waiter);
            disarmTimer(waiter);
        }
        const int result = __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
        if (result < 0) return result;
    }
}

int Endpoint::reply(IpcMessage* message) {
    Operation operation(this);
    if (!validateMessage(message) || !message->replyToken || !getImplementation())
        return failure(os::ErrorCode::InvalidArgument);
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
    PendingCall* call = state.calls;
    while (call && call->token != message->replyToken) call = call->next;
    if (!call || call->serverTask != task::getCurrent() || call->result != waiting ||
        deadlineError(call->deadline) ||
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
    call->result = 0;
    for (auto* waiter = state.callers; waiter; waiter = waiter->next)
        if (waiter->thread == call->caller) complete(*waiter, 0);
    return 0;
}

int Endpoint::call(IpcMessage* request, IpcMessage* reply, uint64_t deadline) {
    Operation operation(this);
    if (!validateMessage(request) || !reply || !getImplementation())
        return failure(os::ErrorCode::InvalidArgument);
    if (auto* service = getImplementation()->service) {
        if (request->handleCount) return failure(os::ErrorCode::InvalidArgument);
        auto& state = *getImplementation();
        Waiter waiter{Thread::getCurrent(), nullptr, nullptr, deadline ? deadline : os::ipcInfiniteDeadline};
        IpcMessage copy = *request;
        stampSender(copy);
        copy.replyToken = nextReplyToken.fetchAdd(1, MemoryOrder::Relaxed);
        {
            InterruptSpinLockGuard guard(state.lock);
            if (state.destroyed) return failure(os::ErrorCode::EndpointClosed);
            if (!hasRightLocked(this, task::getCurrent(), endpointRightSend))
                return failure(os::ErrorCode::PermissionDenied);
            if (deadline && deadlineError(deadline)) return deadlineError(deadline);
            prepareWaiter(state.callers, waiter);
            waiter.thread->setIpcWaitResult(&waiter.result, deadline == 0);
        }
        *reply = {};
        int result = service->call(*task::getCurrent(), copy, *reply);
        {
            InterruptSpinLockGuard guard(state.lock);
            complete(waiter, 0);
            removeWaiter(state.callers, waiter);
            disarmTimer(waiter);
            waiter.thread->setIpcWaitResult(nullptr);
            const int aborted = __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
            if (aborted < 0) result = aborted;
        }
        reply->replyToken = copy.replyToken;
        if (result) {
            for (uint32_t i = 0; i < reply->handleCount; ++i)
                task::getCurrent()->handles().close(reply->handles[i]);
            reply->handleCount = 0;
        }
        return result;
    }
    PendingCall call{Thread::getCurrent(), task::getCurrent(), nullptr, nullptr,
        nextReplyToken.fetchAdd(1, MemoryOrder::Relaxed), reply, deadline};
    IpcMessage copy = *request;
    copy.replyToken = call.token;
    stampSender(copy);
    int result = enqueue(this, copy, &call, deadline);
    if (result) return result;
    auto& state = *getImplementation();
    while (true) {
        Waiter waiter{call.caller, nullptr, nullptr, deadline};
        {
            InterruptSpinLockGuard guard(state.lock);
            if (call.result != waiting) { result = call.result; break; }
            result = deadlineError(deadline);
            if (result) { discardCallMessage(state, call); break; }
            prepareWaiter(state.callers, waiter);
        }
        Thread::blockCurrent();
        {
            InterruptSpinLockGuard guard(state.lock);
            removeWaiter(state.callers, waiter);
            disarmTimer(waiter);
            if (call.result != waiting) result = call.result;
            else result = __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
            if (result < 0) { discardCallMessage(state, call); break; }
        }
    }
    {
        InterruptSpinLockGuard guard(state.lock);
        unlinkCall(state, call);
    }
    return result;
}

int Endpoint::cancel(uint64_t threadId) {
    auto& state = *getImplementation();
    InterruptSpinLockGuard guard(state.lock);
    int result = failure(os::ErrorCode::NotFound);
    for (auto* call = state.calls; call; call = call->next) {
        if (call->caller->getId() == threadId && call->callerTask == task::getCurrent() &&
            call->result == waiting) {
            call->result = failure(os::ErrorCode::Cancelled);
            discardCallMessage(state, *call);
            result = 0;
        }
    }
    Waiter* lists[] = {state.senders, state.receivers, state.callers};
    for (Waiter* list : lists) {
        for (auto* waiter = list; waiter; waiter = waiter->next) {
            if (waiter->thread->getId() != threadId || waiter->thread->getTask() != task::getCurrent()) continue;
            complete(*waiter, failure(os::ErrorCode::Cancelled));
            result = 0;
        }
    }
    return result;
}

void Endpoint::expireWaits(uint64_t now) {
    {
        InterruptSpinLockGuard guard(timerLock);
        while (timers && timers->deadline <= now) {
            Waiter* waiter = timers;
            timers = waiter->timerNext;
            waiter->timerNext = nullptr;
            waiter->timed = false;
            complete(*waiter, failure(os::ErrorCode::Timeout));
        }
    }
    InterruptSpinLockGuard guard(endpointListLock);
    EndpointImplementation** link = &endpoints;
    while (*link) {
        auto* state = *link;
        Endpoint* endpoint = state->endpoint;
        if (__atomic_load_n(&endpoint->references, __ATOMIC_ACQUIRE) != 0) {
            link = &state->next;
            continue;
        }
        *link = state->next;
        while (state->grants) {
            auto* grant = state->grants;
            state->grants = grant->next;
            heap::kernelFree(grant);
        }
        heap::kernelFree(state);
        heap::kernelFree(endpoint);
    }
}

void Endpoint::cancelThread(Thread* thread) {
    {
    InterruptSpinLockGuard listGuard(endpointListLock);
    for (auto* state = endpoints; state; state = state->next) {
        InterruptSpinLockGuard guard(state->lock);
        Waiter** heads[] = {&state->senders, &state->receivers, &state->callers};
        for (Waiter** head : heads) {
            Waiter** link = head;
            while (*link) {
                Waiter* waiter = *link;
                if (waiter->thread != thread) { link = &waiter->next; continue; }
                *link = waiter->next;
                disarmTimer(*waiter);
                complete(*waiter, failure(os::ErrorCode::Interrupted));
            }
        }
        PendingCall** link = &state->calls;
        while (*link) {
            PendingCall* call = *link;
            if (call->serverThread == thread && call->result == waiting) {
                call->result = failure(os::ErrorCode::PeerDied);
                call->serverThread = nullptr;
                for (auto* waiter = state->callers; waiter; waiter = waiter->next)
                    if (waiter->thread == call->caller) complete(*waiter, failure(os::ErrorCode::PeerDied));
            }
            if (call->caller != thread) { link = &call->next; continue; }
            discardCallMessage(*state, *call);
            *link = call->next;
        }
    }
    }
    if (auto* active = thread->getActiveEndpoint()) {
        thread->setActiveEndpoint(nullptr);
        active->release();
    }
    thread->setActiveIpcCall(false);
    thread->setIpcWaitResult(nullptr);
}

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
                if (waiter->thread == thread) complete(*waiter, failure(os::ErrorCode::Interrupted));
    }
}

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
    auto& queued = state.queue[(state.head + state.count) % queueCapacity];
    queued.message = {};
    queued.message.type = type;
    queued.message.length = 1;
    queued.message.data[0] = value;
    ++state.count;
    wakeOne(state.receivers);
    return true;
}
}  // namespace kernel
