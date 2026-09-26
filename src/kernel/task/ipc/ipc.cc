#include "kernel/ipc.h"

#include "kernel/cpu.h"
#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/pipe.h"
#include "kernel/pty.h"
#include "kernel/serial.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"

namespace kernel {

namespace {

constexpr uint32_t queueCapacity = 8;
constexpr uint32_t grantCapacity = 16;

struct PendingCall;

struct QueuedMessage {
    IpcMessage message;
    PendingCall* pendingCall;
};

struct AccessGrant {
    Process* process;
    uint32_t rights;
};

struct PendingCall {
    Thread* caller;
    Process* callerProcess;
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
    AccessGrant grants[grantCapacity];
    bool destroyed;
};

namespace {

uint64_t nextEndpointId = 1;

void clearMemory(void* address, uint64_t size) {
    uint8_t* bytes = static_cast<uint8_t*>(address);
    for (uint64_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool hasRightLocked(const Endpoint* endpoint, const Process* process,
                    uint32_t right) {
    if (endpoint == nullptr || endpoint->implementation == nullptr ||
        process == nullptr) {
        return false;
    }
    if (endpoint->owner == process) {
        return true;
    }
    for (const AccessGrant& grant : endpoint->implementation->grants) {
        if (grant.process == process && (grant.rights & right) == right) {
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
    Process* process   = getCurrentProcess();
    Thread* thread     = getCurrentThread();
    message->senderPid = process == nullptr ? 0 : process->id();
    message->senderTid = thread == nullptr ? 0 : thread->id;
}

void addWaiter(Thread*& waiters, Thread* thread) {
    thread->waitNext = waiters;
    waiters          = thread;
}

void wakeOne(Thread*& waiters) {
    if (waiters == nullptr) {
        return;
    }
    Thread* thread   = waiters;
    waiters          = thread->waitNext;
    thread->waitNext = nullptr;
    wakeThread(thread);
}

bool grantTransferredEndpoint(Endpoint* endpoint, Process* process,
                              uint32_t rights) {
    if (endpoint == nullptr || endpoint->implementation == nullptr ||
        process == nullptr) {
        return false;
    }
    if (endpoint->owner == process) {
        return true;
    }
    EndpointImplementation* implementation = endpoint->implementation;
    InterruptSpinLockGuard guard(implementation->lock);
    if (implementation->destroyed) {
        return false;
    }
    for (AccessGrant& grant : implementation->grants) {
        if (grant.process == process || grant.process == nullptr ||
            grant.process->isTerminated()) {
            grant.process = process;
            grant.rights  = rights & (endpointRightSend | endpointRightReceive |
                                     endpointRightManage);
            return true;
        }
    }
    return false;
}

bool transferHandles(IpcMessage* message, Process* sender, Process* receiver) {
    if (message == nullptr || sender == nullptr || receiver == nullptr) {
        return message != nullptr && message->handleCount == 0;
    }
    for (uint32_t index = 0; index < message->handleCount; ++index) {
        void* object;
        HandleType type;
        uint32_t rights;
        if (!sender->handles().inspect(message->handles[index], &object, &type,
                                       &rights) ||
            (type != HandleType::Endpoint && type != HandleType::Process &&
             type != HandleType::Pipe && type != HandleType::Pty)) {
            return false;
        }
        if (type == HandleType::Endpoint &&
            !grantTransferredEndpoint(static_cast<Endpoint*>(object), receiver,
                                      rights)) {
            return false;
        }
        uint64_t receiverHandle = 0;
        if (type == HandleType::Pipe) {
            receiverHandle = duplicatePipeHandle(*sender, *receiver,
                                                 message->handles[index]);
        } else if (type == HandleType::Pty) {
            receiverHandle = duplicatePtyHandle(*sender, *receiver,
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
                   PendingCall* pendingCall, Process* sender) {
    EndpointImplementation* implementation = endpoint->implementation;
    if (!transferHandles(message, sender, endpoint->owner)) {
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
            addWaiter(implementation->senderWaiters, getCurrentThread());
        }
        blockThread();
    }
}

}  // namespace

void initIpc() { serialWrite("[IPC] synchronous endpoint IPC initialized\n"); }

Endpoint* createEndpoint() {
    Process* owner = getCurrentProcess();
    if (owner == nullptr) {
        return nullptr;
    }
    Endpoint* endpoint =
        static_cast<Endpoint*>(kernelAllocate(sizeof(Endpoint)));
    EndpointImplementation* implementation =
        static_cast<EndpointImplementation*>(
            kernelAllocate(sizeof(EndpointImplementation)));
    if (endpoint == nullptr || implementation == nullptr) {
        return nullptr;
    }
    clearMemory(implementation, sizeof(EndpointImplementation));
    endpoint->id = __atomic_fetch_add(&nextEndpointId, 1ULL, __ATOMIC_RELAXED);
    endpoint->owner          = owner;
    endpoint->implementation = implementation;
    return endpoint;
}

void destroyEndpoint(Endpoint* endpoint) {
    if (endpoint == nullptr || endpoint->implementation == nullptr) {
        return;
    }
    EndpointImplementation* implementation = endpoint->implementation;
    InterruptSpinLockGuard guard(implementation->lock);
    if (!hasRightLocked(endpoint, getCurrentProcess(), endpointRightManage) ||
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
        wakeThread(call->caller);
    }
}

bool grantEndpointAccess(Endpoint* endpoint, Process* process,
                         uint32_t rights) {
    if (endpoint == nullptr || endpoint->implementation == nullptr ||
        process == nullptr) {
        return false;
    }
    EndpointImplementation* implementation = endpoint->implementation;
    InterruptSpinLockGuard guard(implementation->lock);
    if (implementation->destroyed ||
        !hasRightLocked(endpoint, getCurrentProcess(), endpointRightManage)) {
        return false;
    }
    for (AccessGrant& grant : implementation->grants) {
        if (grant.process == process || grant.process == nullptr ||
            grant.process->isTerminated()) {
            grant.process = process;
            grant.rights  = rights & (endpointRightSend | endpointRightReceive |
                                     endpointRightManage);
            return true;
        }
    }
    return false;
}

uint64_t duplicateEndpointHandle(Process* source, Process* destination,
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

int sendMessage(Endpoint* endpoint, IpcMessage* message) {
    if (!validateMessage(message) || endpoint == nullptr ||
        endpoint->implementation == nullptr) {
        return -1;
    }
    IpcMessage copy = *message;
    stampSender(&copy);
    return enqueueMessage(endpoint, &copy, nullptr, getCurrentProcess());
}

int receiveMessage(Endpoint* endpoint, IpcMessage* message) {
    if (message == nullptr || endpoint == nullptr ||
        endpoint->implementation == nullptr) {
        return -1;
    }
    EndpointImplementation* implementation = endpoint->implementation;
    while (true) {
        {
            InterruptSpinLockGuard guard(implementation->lock);
            if (implementation->destroyed ||
                !hasRightLocked(endpoint, getCurrentProcess(),
                                endpointRightReceive)) {
                return -1;
            }
            if (implementation->count != 0) {
                QueuedMessage& queued =
                    implementation->queue[implementation->head];
                *message = queued.message;
                if (queued.pendingCall != nullptr) {
                    queued.pendingCall->server = getCurrentThread();
                }
                queued.pendingCall = nullptr;
                implementation->head =
                    (implementation->head + 1) % queueCapacity;
                --implementation->count;
                wakeOne(implementation->senderWaiters);
                return 0;
            }
            addWaiter(implementation->receiverWaiters, getCurrentThread());
        }
        blockThread();
    }
}

int replyMessage(Endpoint* endpoint, IpcMessage* message) {
    if (!validateMessage(message) || endpoint == nullptr ||
        endpoint->implementation == nullptr) {
        return -1;
    }
    EndpointImplementation* implementation = endpoint->implementation;
    PendingCall* call;
    {
        InterruptSpinLockGuard guard(implementation->lock);
        if (implementation->destroyed ||
            !hasRightLocked(endpoint, getCurrentProcess(),
                            endpointRightReceive)) {
            return -1;
        }
        PendingCall** link = &implementation->pendingCalls;
        while (*link != nullptr && (*link)->server != getCurrentThread()) {
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
        transferHandles(&reply, getCurrentProcess(), call->callerProcess) ? 0
                                                                          : -1;
    if (call->result == 0) {
        *call->replyTarget = reply;
    }
    __atomic_store_n(&call->completed, true, __ATOMIC_RELEASE);
    wakeThread(call->caller);
    return 0;
}

int callService(Endpoint* endpoint, IpcMessage* request, IpcMessage* reply) {
    if (!validateMessage(request) || reply == nullptr || endpoint == nullptr ||
        endpoint->implementation == nullptr) {
        return -1;
    }
    PendingCall callStorage{};
    PendingCall* call = &callStorage;
    call->caller        = getCurrentThread();
    call->callerProcess = getCurrentProcess();
    call->replyTarget   = reply;
    IpcMessage copy     = *request;
    stampSender(&copy);
    const int result =
        enqueueMessage(endpoint, &copy, call, getCurrentProcess());
    if (result != 0) {
        return result;
    }
    while (!__atomic_load_n(&call->completed, __ATOMIC_ACQUIRE)) {
        blockThread();
    }
    return call->result;
}

bool notifyEndpointFromInterrupt(Endpoint* endpoint, uint64_t type,
                                 uint64_t value) {
    if (endpoint == nullptr || endpoint->implementation == nullptr) {
        return false;
    }
    EndpointImplementation* implementation = endpoint->implementation;
    implementation->lock.lock();
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
