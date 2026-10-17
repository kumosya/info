#include "kernel/ipc.h"

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"
#include "kernel/thread.h"

namespace kernel {

namespace {

constexpr uint32_t queueCapacity = 8;

struct PendingCall;

struct QueuedMessage {
    IpcMessage message;
    PendingCall* pendingCall;
};

struct AccessGrant {
    Task* task;
    uint32_t rights;
    AccessGrant* next;
};

struct PendingCall {
    Thread* caller;
    Task* callerTask;
    Thread* server;
    IpcMessage* replyTarget;
    PendingCall* next;
    int result;
    bool completed;
};

}  // namespace

struct EndpointImplementation {
    SpinLock lock;
    QueuedMessage queue[queueCapacity];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    Thread* receiverWaiters;
    Thread* senderWaiters;
    PendingCall* pendingCalls;
    AccessGrant* grants;
    bool destroyed;
    uint16_t pendingIrqs;
};

namespace {

uint64_t nextEndpointId = 1;

void clearMemory(void* address, uint64_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(address);
    for (uint64_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool hasRightLocked(const Endpoint* endpoint, const Task* task,
                    uint32_t right) {
    if (endpoint == nullptr || endpoint->getImplementation() == nullptr ||
        task == nullptr) {
        return false;
    }
    if (endpoint->getOwner() == task) {
        return true;
    }
    for (const AccessGrant* grant = endpoint->getImplementation()->grants;
         grant != nullptr; grant = grant->next) {
        if (grant->task == task && (grant->rights & right) == right) {
            return true;
        }
    }
    return false;
}

bool validateMessage(const IpcMessage* message) {
    return message != nullptr && message->length <= os::ipcDataWordCount &&
           message->handleCount <= 4;
}

void stampSender(IpcMessage* message) {
    Task* task         = task::getCurrent();
    Thread* thread     = Thread::getCurrent();
    message->senderPid = task == nullptr ? 0 : task->id();
    message->senderTid = thread == nullptr ? 0 : thread->getId();
}

void addWaiter(Thread*& waiters, Thread* thread) {
    Thread::prepareWait();
    thread->setWaitNext(waiters);
    waiters = thread;
}

void wakeOne(Thread*& waiters) {
    if (waiters == nullptr) {
        return;
    }
    Thread* thread = waiters;
    waiters        = thread->getWaitNext();
    thread->setWaitNext(nullptr);
    Thread::wake(thread);
}

bool grantTransferredEndpoint(Endpoint* endpoint, Task* task, uint32_t rights) {
    if (endpoint == nullptr || endpoint->getImplementation() == nullptr ||
        task == nullptr) {
        return false;
    }
    if (endpoint->getOwner() == task) {
        return true;
    }
    EndpointImplementation* implementation = endpoint->getImplementation();
    InterruptSpinLockGuard guard(implementation->lock);
    if (implementation->destroyed) {
        return false;
    }
    AccessGrant* reusable = nullptr;
    for (AccessGrant* grant = implementation->grants; grant != nullptr;
         grant = grant->next) {
        if (grant->task == task) {
            grant->rights |= rights;
            return true;
        }
        if (grant->task == nullptr || grant->task->isTerminated())
            reusable = grant;
    }
    if (reusable == nullptr) {
        reusable = static_cast<AccessGrant*>(heap::kernelAllocate(sizeof(AccessGrant)));
        if (reusable == nullptr) return false;
        reusable->next = implementation->grants;
        implementation->grants = reusable;
    }
    reusable->task = task;
    reusable->rights = rights & (endpointRightSend | endpointRightReceive |
                                endpointRightManage);
    return true;
}

bool transferHandles(IpcMessage* message, Task* sender, Task* receiver) {
    if (message == nullptr || sender == nullptr || receiver == nullptr) {
        return message != nullptr && message->handleCount == 0;
    }
    for (uint32_t index = 0; index < message->handleCount; ++index) {
        void* object;
        HandleType type;
        uint32_t rights;
        if (!sender->handles().inspect(message->handles[index], &object, &type,
                                       &rights) ||
            (type != HandleType::Endpoint && type != HandleType::Task &&
             type != HandleType::Pipe && type != HandleType::Channel)) {
            return false;
        }
        if (type == HandleType::Endpoint &&
            !grantTransferredEndpoint(static_cast<Endpoint*>(object), receiver,
                                      rights)) {
            return false;
        }
        uint64_t receiverHandle = 0;
        if (type == HandleType::Pipe || type == HandleType::Channel) {
            receiverHandle = Pipe::duplicateHandle(*sender, *receiver,
                                                   message->handles[index]);
        } else {
            receiverHandle = receiver->handles().add(object, type, rights);
        }
        if (receiverHandle == 0) {
            return false;
        }
        message->handles[index] = receiverHandle;
    }
    return true;
}

int enqueueMessage(Endpoint* endpoint, IpcMessage* message,
                   PendingCall* pendingCall, Task* sender) {
    EndpointImplementation* implementation = endpoint->getImplementation();
    if (!transferHandles(message, sender, endpoint->getOwner())) {
        return -1;
    }
    while (true) {
        {
            InterruptSpinLockGuard guard(implementation->lock);
            if (implementation->destroyed ||
                !hasRightLocked(endpoint, sender, endpointRightSend)) {
                return -1;
            }
            if (implementation->count < queueCapacity) {
                QueuedMessage& queued =
                    implementation->queue[implementation->tail];
                queued.message     = *message;
                queued.pendingCall = pendingCall;
                implementation->tail =
                    (implementation->tail + 1) % queueCapacity;
                ++implementation->count;
                if (pendingCall != nullptr) {
                    pendingCall->next            = implementation->pendingCalls;
                    implementation->pendingCalls = pendingCall;
                }
                wakeOne(implementation->receiverWaiters);
                return 0;
            }
            addWaiter(implementation->senderWaiters, Thread::getCurrent());
        }
        Thread::blockCurrent();
    }
}

}  // namespace

void Endpoint::init() {
    serial::serialWrite("[IPC] synchronous endpoint IPC initialized\n");
}

Endpoint* Endpoint::create() {
    Task* owner = task::getCurrent();
    if (owner == nullptr) {
        return nullptr;
    }
    Endpoint* endpoint =
        static_cast<Endpoint*>(heap::kernelAllocate(sizeof(Endpoint)));
    EndpointImplementation* implementation =
        static_cast<EndpointImplementation*>(
            heap::kernelAllocate(sizeof(EndpointImplementation)));
    if (endpoint == nullptr || implementation == nullptr) {
        return nullptr;
    }
    clearMemory(implementation, sizeof(EndpointImplementation));
    endpoint->setId(
        __atomic_fetch_add(&nextEndpointId, 1ULL, __ATOMIC_RELAXED));
    endpoint->setOwner(owner);
    endpoint->setImplementation(implementation);
    return endpoint;
}

void Endpoint::destroy() {
    Endpoint* endpoint = this;
    if (endpoint->getImplementation() == nullptr) {
        return;
    }
    EndpointImplementation* implementation = endpoint->getImplementation();
    InterruptSpinLockGuard guard(implementation->lock);
    if (!hasRightLocked(endpoint, task::getCurrent(), endpointRightManage) ||
        implementation->destroyed) {
        return;
    }
    implementation->destroyed = true;
    while (implementation->receiverWaiters != nullptr) {
        wakeOne(implementation->receiverWaiters);
    }
    while (implementation->senderWaiters != nullptr) {
        wakeOne(implementation->senderWaiters);
    }
    for (PendingCall* call = implementation->pendingCalls; call != nullptr;
         call              = call->next) {
        call->result = -1;
        __atomic_store_n(&call->completed, true, __ATOMIC_RELEASE);
        Thread::wake(call->caller);
    }
}

bool Endpoint::grantAccess(Task* task, uint32_t rights) {
    Endpoint* endpoint = this;
    if (endpoint->getImplementation() == nullptr || task == nullptr) {
        return false;
    }
    {
        EndpointImplementation* implementation = endpoint->getImplementation();
        InterruptSpinLockGuard guard(implementation->lock);
        if (implementation->destroyed ||
            !hasRightLocked(endpoint, task::getCurrent(), endpointRightManage))
            return false;
    }
    return grantTransferredEndpoint(endpoint, task, rights);
}

uint64_t Endpoint::duplicateHandle(Task* source, Task* destination,
                                   uint64_t handle,
                                   uint64_t destinationHandle) {
    if (source == nullptr || destination == nullptr) {
        return 0;
    }
    void* object;
    HandleType type;
    uint32_t rights;
    if (!source->handles().inspect(handle, &object, &type, &rights) ||
        type != HandleType::Endpoint ||
        !grantTransferredEndpoint(static_cast<Endpoint*>(object), destination,
                                  rights)) {
        return 0;
    }
    if (destinationHandle != 0) {
        return destination->handles().addAt(destinationHandle, object, type,
                                            rights)
                   ? destinationHandle
                   : 0;
    }
    return destination->handles().add(object, type, rights);
}

int Endpoint::send(IpcMessage* message) {
    Endpoint* endpoint = this;
    if (!validateMessage(message) || endpoint->getImplementation() == nullptr) {
        return -1;
    }
    IpcMessage copy = *message;
    stampSender(&copy);
    return enqueueMessage(endpoint, &copy, nullptr, task::getCurrent());
}

int Endpoint::receive(IpcMessage* message) {
    Endpoint* endpoint = this;
    if (message == nullptr || endpoint->getImplementation() == nullptr) {
        return -1;
    }
    EndpointImplementation* implementation = endpoint->getImplementation();
    while (true) {
        {
            InterruptSpinLockGuard guard(implementation->lock);
            if (implementation->destroyed ||
                !hasRightLocked(endpoint, task::getCurrent(),
                                endpointRightReceive)) {
                return -1;
            }
            if (implementation->pendingIrqs != 0) {
                const uint32_t irq = __builtin_ctz(implementation->pendingIrqs);
                implementation->pendingIrqs &= static_cast<uint16_t>(~(1U << irq));
                *message = {};
                message->type = 0x495251;
                message->length = 1;
                message->data[0] = irq;
                return 0;
            }
            if (implementation->count != 0) {
                QueuedMessage& queued =
                    implementation->queue[implementation->head];
                *message = queued.message;
                if (queued.pendingCall != nullptr) {
                    queued.pendingCall->server = Thread::getCurrent();
                }
                queued.pendingCall = nullptr;
                implementation->head =
                    (implementation->head + 1) % queueCapacity;
                --implementation->count;
                wakeOne(implementation->senderWaiters);
                return 0;
            }
            addWaiter(implementation->receiverWaiters, Thread::getCurrent());
        }
        Thread::blockCurrent();
    }
}

int Endpoint::reply(IpcMessage* message) {
    Endpoint* endpoint = this;
    if (!validateMessage(message) || endpoint->getImplementation() == nullptr) {
        return -1;
    }
    EndpointImplementation* implementation = endpoint->getImplementation();
    PendingCall* call;
    {
        InterruptSpinLockGuard guard(implementation->lock);
        if (implementation->destroyed ||
            !hasRightLocked(endpoint, task::getCurrent(),
                            endpointRightReceive)) {
            return -1;
        }
        PendingCall** link = &implementation->pendingCalls;
        while (*link != nullptr && (*link)->server != Thread::getCurrent()) {
            link = &(*link)->next;
        }
        if (*link == nullptr) {
            return -1;
        }
        call  = *link;
        *link = call->next;
    }
    IpcMessage reply = *message;
    stampSender(&reply);
    call->result =
        transferHandles(&reply, task::getCurrent(), call->callerTask) ? 0 : -1;
    if (call->result == 0) {
        *call->replyTarget = reply;
    }
    __atomic_store_n(&call->completed, true, __ATOMIC_RELEASE);
    Thread::wake(call->caller);
    return 0;
}

int Endpoint::call(IpcMessage* request, IpcMessage* reply) {
    Endpoint* endpoint = this;
    if (!validateMessage(request) || reply == nullptr ||
        endpoint->getImplementation() == nullptr) {
        return -1;
    }
    PendingCall callStorage{};
    PendingCall* call = &callStorage;
    call->caller      = Thread::getCurrent();
    call->callerTask  = task::getCurrent();
    call->replyTarget = reply;
    call->caller->setActiveIpcCall(true);
    IpcMessage copy   = *request;
    stampSender(&copy);
    const int result =
        enqueueMessage(endpoint, &copy, call, task::getCurrent());
    if (result != 0) {
        call->caller->setActiveIpcCall(false);
        return result;
    }
    while (!__atomic_load_n(&call->completed, __ATOMIC_ACQUIRE)) {
        Thread::blockCurrent();
    }
    call->caller->setActiveIpcCall(false);
    return call->result;
}

bool Endpoint::notifyFromInterrupt(uint64_t type, uint64_t value) {
    Endpoint* endpoint = this;
    if (endpoint->getImplementation() == nullptr) {
        return false;
    }
    EndpointImplementation* implementation = endpoint->getImplementation();
    implementation->lock.lock();
    if (!implementation->destroyed && type == 0x495251 && value < 16) {
        implementation->pendingIrqs |= static_cast<uint16_t>(1U << value);
        wakeOne(implementation->receiverWaiters);
        implementation->lock.unlock();
        return true;
    }
    if (implementation->destroyed || implementation->count == queueCapacity) {
        implementation->lock.unlock();
        return false;
    }
    QueuedMessage& queued = implementation->queue[implementation->tail];
    clearMemory(&queued, sizeof(queued));
    queued.message.type    = type;
    queued.message.length  = 1;
    queued.message.data[0] = value;
    implementation->tail   = (implementation->tail + 1) % queueCapacity;
    ++implementation->count;
    wakeOne(implementation->receiverWaiters);
    implementation->lock.unlock();
    return true;
}

}  // namespace kernel
