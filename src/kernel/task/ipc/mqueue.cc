#include "kernel/cpu.h"
#include "kernel/ipc.h"
#include "kernel/signal.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"
#include "kernel/thread.h"

namespace {

constexpr uint32_t queueCapacity       = 16;
constexpr uint32_t handleCapacity      = 64;
constexpr uint32_t messageCapacity     = 16;
constexpr uint32_t messageSizeCapacity = 256;
constexpr uint32_t nameCapacity        = 64;
constexpr int openCreate               = 0x40;
constexpr int openExclusive            = 0x80;
constexpr int openNonblocking          = 0x800;

struct Message {
    uint32_t size;
    uint32_t priority;
    uint8_t data[messageSizeCapacity];
};

struct MessageQueueHandle;

struct MessageQueueState {
    bool used;
    bool linked;
    char name[nameCapacity];
    uint32_t references;
    uint32_t maximumMessages;
    uint32_t messageSize;
    uint32_t count;
    Message messages[messageCapacity];
    kernel::Thread* senders;
    kernel::Thread* sendersTail;
    kernel::Thread* receivers;
    kernel::Thread* receiversTail;
    uint64_t notificationTask;
    uint32_t notificationSignal;
    bool notificationRegistered;
    MessageQueueHandle* notificationHandle;
    kernel::SpinLock lock;
};

struct MessageQueueHandle {
    bool used;
    bool nonblocking;
    MessageQueueState* queue;
};

MessageQueueState queues[queueCapacity];
MessageQueueHandle handles[handleCapacity];
kernel::SpinLock registryLock;

bool namesEqual(const char* left, const char* right) {
    uint32_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

void copyName(char* destination, const char* source) {
    uint32_t index = 0;
    while (index + 1 < nameCapacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void append(kernel::Thread*& head, kernel::Thread*& tail,
            kernel::Thread* thread) {
    kernel::Thread::prepareWait();
    thread->setWaitNext(nullptr);
    if (tail == nullptr)
        head = thread;
    else
        tail->setWaitNext(thread);
    tail = thread;
}

kernel::Thread* remove(kernel::Thread*& head, kernel::Thread*& tail) {
    kernel::Thread* result = head;
    if (result == nullptr) return nullptr;
    head = result->getWaitNext();
    if (head == nullptr) tail = nullptr;
    result->setWaitNext(nullptr);
    return result;
}

MessageQueueHandle* resolve(kernel::Task& task, uint64_t handle,
                            uint32_t rights) {
    return static_cast<MessageQueueHandle*>(
        task.handles().get(handle, kernel::HandleType::MessageQueue, rights));
}

void clearQueue(MessageQueueState& queue) {
    queue.used                   = false;
    queue.linked                 = false;
    queue.name[0]                = '\0';
    queue.references             = 0;
    queue.count                  = 0;
    queue.senders                = nullptr;
    queue.sendersTail            = nullptr;
    queue.receivers              = nullptr;
    queue.receiversTail          = nullptr;
    queue.notificationTask       = 0;
    queue.notificationSignal     = 0;
    queue.notificationRegistered = false;
    queue.notificationHandle     = nullptr;
}

}  // namespace

namespace kernel {

uint64_t MessageQueue::open(Task& task, const char* name, int flags,
                            uint32_t maximumMessages, uint32_t messageSize) {
    if (name == nullptr || name[0] != '/' || name[1] == '\0') return 0;
    InterruptSpinLockGuard registryGuard(registryLock);
    MessageQueueState* queue = nullptr;
    for (MessageQueueState& candidate : queues) {
        if (candidate.used && candidate.linked &&
            namesEqual(candidate.name, name)) {
            queue = &candidate;
            break;
        }
    }
    if (queue != nullptr && (flags & openCreate) != 0 &&
        (flags & openExclusive) != 0)
        return 0;
    if (queue == nullptr) {
        if ((flags & openCreate) == 0 || maximumMessages == 0 ||
            maximumMessages > messageCapacity || messageSize == 0 ||
            messageSize > messageSizeCapacity)
            return 0;
        for (MessageQueueState& candidate : queues) {
            if (!candidate.used) {
                queue = &candidate;
                clearQueue(*queue);
                queue->used   = true;
                queue->linked = true;
                copyName(queue->name, name);
                queue->maximumMessages = maximumMessages;
                queue->messageSize     = messageSize;
                break;
            }
        }
    }
    if (queue == nullptr) return 0;

    MessageQueueHandle* queueHandle = nullptr;
    for (MessageQueueHandle& candidate : handles) {
        if (!candidate.used) {
            queueHandle           = &candidate;
            candidate.used        = true;
            candidate.nonblocking = (flags & openNonblocking) != 0;
            candidate.queue       = queue;
            break;
        }
    }
    if (queueHandle == nullptr) return 0;
    ++queue->references;
    uint32_t rights = handleRightRead | handleRightWrite;
    const uint64_t result =
        task.handles().add(queueHandle, HandleType::MessageQueue, rights);
    if (result == 0) {
        queueHandle->used  = false;
        queueHandle->queue = nullptr;
        --queue->references;
    }
    return result;
}

uint64_t MessageQueue::duplicateHandle(Task& source, Task& destination,
                                       uint64_t handle,
                                       uint64_t destinationHandle) {
    void* object    = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!source.handles().inspect(handle, &object, &type, &rights) ||
        type != HandleType::MessageQueue) {
        return 0;
    }
    auto* original = static_cast<MessageQueueHandle*>(object);
    InterruptSpinLockGuard guard(registryLock);
    if (!original->used || original->queue == nullptr) return 0;
    MessageQueueHandle* duplicate = nullptr;
    for (MessageQueueHandle& candidate : handles) {
        if (!candidate.used) {
            duplicate              = &candidate;
            duplicate->used        = true;
            duplicate->nonblocking = original->nonblocking;
            duplicate->queue       = original->queue;
            break;
        }
    }
    if (duplicate == nullptr) return 0;
    ++duplicate->queue->references;
    const uint64_t result =
        destinationHandle == 0
            ? destination.handles().add(duplicate, HandleType::MessageQueue,
                                        rights)
            : (destination.handles().addAt(destinationHandle, duplicate,
                                           HandleType::MessageQueue, rights)
                   ? destinationHandle
                   : 0);
    if (result == 0) {
        if (duplicate->queue->references != 0) --duplicate->queue->references;
        duplicate->used  = false;
        duplicate->queue = nullptr;
    }
    return result;
}

bool MessageQueue::unlink(const char* name) {
    if (name == nullptr) return false;
    InterruptSpinLockGuard guard(registryLock);
    for (MessageQueueState& queue : queues) {
        if (!queue.used || !queue.linked || !namesEqual(queue.name, name)) {
            continue;
        }
        queue.linked  = false;
        queue.name[0] = '\0';
        if (queue.references == 0) clearQueue(queue);
        return true;
    }
    return false;
}

int MessageQueue::send(Task& task, uint64_t handle, const void* data,
                       size_t size, uint32_t priority) {
    MessageQueueHandle* queueHandle = resolve(task, handle, handleRightWrite);
    if (queueHandle == nullptr || data == nullptr || priority > 31) return -1;
    MessageQueueState* queue = queueHandle->queue;
    if (size > queue->messageSize) return -1;
    while (true) {
        Thread* receiver            = nullptr;
        uint64_t notificationTask   = 0;
        uint32_t notificationSignal = 0;
        const uint64_t flags        = interrupt::disable();
        queue->lock.lock();
        if (queue->count < queue->maximumMessages) {
            const bool wasEmpty = queue->count == 0;
            Message& message    = queue->messages[queue->count++];
            message.size        = static_cast<uint32_t>(size);
            message.priority    = priority;
            for (size_t index = 0; index < size; ++index) {
                message.data[index] = static_cast<const uint8_t*>(data)[index];
            }
            receiver = remove(queue->receivers, queue->receiversTail);
            if (wasEmpty && receiver == nullptr &&
                queue->notificationRegistered) {
                notificationTask              = queue->notificationTask;
                notificationSignal            = queue->notificationSignal;
                queue->notificationTask       = 0;
                queue->notificationSignal     = 0;
                queue->notificationRegistered = false;
                queue->notificationHandle     = nullptr;
            }
            queue->lock.unlock();
            if (receiver != nullptr) Thread::wake(receiver);
            interrupt::restore(flags);
            if (notificationTask != 0 && notificationSignal != 0) {
                signal::sendSignalToTask(notificationTask, notificationSignal);
            }
            return 0;
        }
        if (queueHandle->nonblocking) {
            queue->lock.unlock();
            interrupt::restore(flags);
            return 1;
        }
        append(queue->senders, queue->sendersTail, Thread::getCurrent());
        queue->lock.unlock();
        Thread::blockCurrent();
        interrupt::restore(flags);
    }
}

int64_t MessageQueue::receive(Task& task, uint64_t handle, void* data,
                              size_t capacity, uint32_t& priority) {
    MessageQueueHandle* queueHandle = resolve(task, handle, handleRightRead);
    if (queueHandle == nullptr || data == nullptr) return -1;
    MessageQueueState* queue = queueHandle->queue;
    if (capacity < queue->messageSize) return -1;
    while (true) {
        Thread* sender       = nullptr;
        const uint64_t flags = interrupt::disable();
        queue->lock.lock();
        if (queue->count != 0) {
            uint32_t selected = 0;
            for (uint32_t index = 1; index < queue->count; ++index) {
                if (queue->messages[index].priority >
                    queue->messages[selected].priority)
                    selected = index;
            }
            Message message = queue->messages[selected];
            for (uint32_t index = selected + 1; index < queue->count; ++index) {
                queue->messages[index - 1] = queue->messages[index];
            }
            --queue->count;
            sender = remove(queue->senders, queue->sendersTail);
            queue->lock.unlock();
            for (uint32_t index = 0; index < message.size; ++index) {
                static_cast<uint8_t*>(data)[index] = message.data[index];
            }
            priority = message.priority;
            if (sender != nullptr) Thread::wake(sender);
            interrupt::restore(flags);
            return message.size;
        }
        if (queueHandle->nonblocking) {
            queue->lock.unlock();
            interrupt::restore(flags);
            return -2;
        }
        append(queue->receivers, queue->receiversTail, Thread::getCurrent());
        queue->lock.unlock();
        Thread::blockCurrent();
        interrupt::restore(flags);
    }
}

bool MessageQueue::getAttributes(Task& task, uint64_t handle,
                                 MessageQueueAttributes& attributes) {
    MessageQueueHandle* queueHandle = resolve(task, handle, 0);
    if (queueHandle == nullptr) return false;
    MessageQueueState* queue = queueHandle->queue;
    InterruptSpinLockGuard guard(queue->lock);
    attributes = {queueHandle->nonblocking ? 0x800ULL : 0,
                  queue->maximumMessages, queue->messageSize, queue->count};
    return true;
}

bool MessageQueue::setFlags(Task& task, uint64_t handle, uint64_t flags,
                            uint64_t& oldFlags) {
    MessageQueueHandle* queueHandle = resolve(task, handle, 0);
    if (queueHandle == nullptr || (flags & ~0x800ULL) != 0) return false;
    oldFlags                 = queueHandle->nonblocking ? 0x800ULL : 0;
    queueHandle->nonblocking = (flags & 0x800ULL) != 0;
    return true;
}

int MessageQueue::registerNotification(Task& task, uint64_t handle, int type,
                                       uint32_t signal) {
    MessageQueueHandle* queueHandle = resolve(task, handle, 0);
    if (queueHandle == nullptr || type < 0 || type > 2 ||
        (type == 2 && !signal::isValidSignal(signal)))
        return -1;
    MessageQueueState* queue = queueHandle->queue;
    InterruptSpinLockGuard guard(queue->lock);
    if (type == 0) {
        if (queue->notificationRegistered &&
            queue->notificationTask != task.id())
            return 1;
        queue->notificationTask       = 0;
        queue->notificationSignal     = 0;
        queue->notificationRegistered = false;
        queue->notificationHandle     = nullptr;
        return 0;
    }
    if (queue->notificationRegistered) return 1;
    queue->notificationTask       = task.id();
    queue->notificationSignal     = type == 2 ? signal : 0;
    queue->notificationRegistered = true;
    queue->notificationHandle     = queueHandle;
    return 0;
}

void MessageQueue::closeHandleObject(void* object) {
    auto* queueHandle = static_cast<MessageQueueHandle*>(object);
    if (queueHandle == nullptr || !queueHandle->used) return;
    InterruptSpinLockGuard guard(registryLock);
    MessageQueueState* queue = queueHandle->queue;
    if (queue != nullptr) {
        InterruptSpinLockGuard queueGuard(queue->lock);
        if (queue->notificationHandle == queueHandle) {
            queue->notificationTask       = 0;
            queue->notificationSignal     = 0;
            queue->notificationRegistered = false;
            queue->notificationHandle     = nullptr;
        }
    }
    queueHandle->used  = false;
    queueHandle->queue = nullptr;
    if (queue != nullptr && queue->references != 0) --queue->references;
    if (queue != nullptr && queue->references == 0 && !queue->linked) {
        clearQueue(*queue);
    }
}

}  // namespace kernel
