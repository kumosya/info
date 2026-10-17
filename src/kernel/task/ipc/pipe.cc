#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/spinlock.h"
#include "kernel/task.h"
#include "kernel/thread.h"

namespace kernel {

namespace {

constexpr uint32_t pipeCapacity = 16 * 1024;

struct PipeState {
    SpinLock lock;
    uint8_t buffer[pipeCapacity];
    uint32_t head;
    uint32_t tail;
    uint32_t size;
    uint32_t eofEvents;
    uint32_t readers;
    uint32_t writers;
    Thread* readerWaiters;
    Thread* writerWaiters;
};

struct PipeEnd {
    PipeState* pipe;
    bool readable;
    bool closed;
};

struct ChannelEnd {
    PipeEnd* input;
    PipeEnd* output;
    uint64_t identity;
    bool* nonblocking;
    bool closed;
};

uint64_t nextChannelIdentity = 1;

void clearMemory(void* address, size_t size) {
    auto* bytes = static_cast<uint8_t*>(address);
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
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

void wakeAll(Thread*& waiters) {
    while (waiters != nullptr) {
        wakeOne(waiters);
    }
}

PipeEnd* createEnd(PipeState* pipe, bool readable) {
    auto* end = static_cast<PipeEnd*>(heap::kernelAllocate(sizeof(PipeEnd)));
    if (end != nullptr) {
        *end = {pipe, readable, false};
    }
    return end;
}

PipeState* createState() {
    auto* state = static_cast<PipeState*>(heap::kernelAllocate(sizeof(PipeState)));
    if (state == nullptr) return nullptr;
    clearMemory(state, sizeof(PipeState));
    state->lock.init();
    state->readers = 1;
    state->writers = 1;
    return state;
}

ChannelEnd* createChannelEnd(PipeEnd* input, PipeEnd* output,
                             uint64_t identity, bool* nonblocking) {
    auto* end = static_cast<ChannelEnd*>(heap::kernelAllocate(sizeof(ChannelEnd)));
    if (end != nullptr) *end = {input, output, identity, nonblocking, false};
    return end;
}

}  // namespace

bool Pipe::createHandles(Task& task, uint64_t& readHandle,
                         uint64_t& writeHandle) {
    readHandle  = 0;
    writeHandle = 0;
    auto* pipe = createState();
    if (pipe == nullptr) {
        return false;
    }
    PipeEnd* readEnd  = createEnd(pipe, true);
    PipeEnd* writeEnd = createEnd(pipe, false);
    if (readEnd == nullptr || writeEnd == nullptr) {
        return false;
    }
    readHandle = task.handles().add(readEnd, HandleType::Pipe, handleRightRead);
    writeHandle =
        task.handles().add(writeEnd, HandleType::Pipe, handleRightWrite);
    if (readHandle == 0 || writeHandle == 0) {
        if (readHandle != 0) {
            task.handles().close(readHandle);
        } else {
            Pipe::closeHandleObject(readEnd);
        }
        if (writeHandle != 0) {
            task.handles().close(writeHandle);
        } else {
            Pipe::closeHandleObject(writeEnd);
        }
        readHandle  = 0;
        writeHandle = 0;
        return false;
    }
    return true;
}

bool Pipe::createChannelPair(Task& task, uint64_t& firstHandle,
                             uint64_t& secondHandle) {
    firstHandle = secondHandle = 0;
    PipeState* firstInput = createState();
    PipeState* secondInput = createState();
    if (firstInput == nullptr || secondInput == nullptr) return false;
    PipeEnd* firstRead = createEnd(firstInput, true);
    PipeEnd* secondWrite = createEnd(firstInput, false);
    PipeEnd* secondRead = createEnd(secondInput, true);
    PipeEnd* firstWrite = createEnd(secondInput, false);
    if (!firstRead || !secondWrite || !secondRead || !firstWrite) return false;
    const uint64_t firstId = __atomic_fetch_add(&nextChannelIdentity, 1ULL,
                                                 __ATOMIC_RELAXED);
    const uint64_t secondId = __atomic_fetch_add(&nextChannelIdentity, 1ULL,
                                                  __ATOMIC_RELAXED);
    auto* firstFlags = static_cast<bool*>(heap::kernelAllocate(sizeof(bool)));
    auto* secondFlags = static_cast<bool*>(heap::kernelAllocate(sizeof(bool)));
    if (!firstFlags || !secondFlags) return false;
    *firstFlags = *secondFlags = false;
    ChannelEnd* first = createChannelEnd(firstRead, firstWrite, firstId,
                                         firstFlags);
    ChannelEnd* second = createChannelEnd(secondRead, secondWrite, secondId,
                                          secondFlags);
    if (!first || !second) return false;
    firstHandle = task.handles().add(first, HandleType::Channel,
                                    handleRightRead | handleRightWrite);
    secondHandle = task.handles().add(second, HandleType::Channel,
                                     handleRightRead | handleRightWrite);
    if (!firstHandle || !secondHandle) {
        if (firstHandle) task.handles().close(firstHandle);
        else closeChannelHandleObject(first);
        if (secondHandle) task.handles().close(secondHandle);
        else closeChannelHandleObject(second);
        firstHandle = secondHandle = 0;
        return false;
    }
    return true;
}

uint64_t Pipe::channelIdentity(Task& task, uint64_t handle) {
    auto* end = static_cast<ChannelEnd*>(
        task.handles().get(handle, HandleType::Channel, handleRightRead));
    return end && !end->closed ? end->identity : 0;
}

bool Pipe::setChannelNonblocking(Task& task, uint64_t handle, bool enabled) {
    auto* end = static_cast<ChannelEnd*>(
        task.handles().get(handle, HandleType::Channel, handleRightRead));
    if (!end || end->closed) return false;
    __atomic_store_n(end->nonblocking, enabled, __ATOMIC_RELEASE);
    return true;
}

bool Pipe::signalChannelEof(Task& task, uint64_t handle) {
    auto* end = static_cast<ChannelEnd*>(
        task.handles().get(handle, HandleType::Channel, handleRightWrite));
    if (!end || end->closed) return false;
    PipeState* state = end->output->pipe;
    InterruptSpinLockGuard guard(state->lock);
    ++state->eofEvents;
    wakeOne(state->readerWaiters);
    return true;
}

uint64_t Pipe::duplicateHandle(Task& source, Task& destination, uint64_t handle,
                               uint64_t destinationHandle) {
    void* object    = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!source.handles().inspect(handle, &object, &type, &rights) ||
        (type != HandleType::Pipe && type != HandleType::Channel)) {
        return 0;
    }
    if (type == HandleType::Channel) {
        auto* original = static_cast<ChannelEnd*>(object);
        if (original->closed) return 0;
        PipeEnd* input = nullptr;
        PipeEnd* output = nullptr;
        {
            InterruptSpinLockGuard guard(original->input->pipe->lock);
            input = createEnd(original->input->pipe, true);
            if (!input) return 0;
            ++input->pipe->readers;
        }
        {
            InterruptSpinLockGuard guard(original->output->pipe->lock);
            output = createEnd(original->output->pipe, false);
            if (!output) {
                closeHandleObject(input);
                return 0;
            }
            ++output->pipe->writers;
        }
        ChannelEnd* duplicate = createChannelEnd(input, output,
                                                  original->identity,
                                                  original->nonblocking);
        if (!duplicate) {
            closeHandleObject(input);
            closeHandleObject(output);
            return 0;
        }
        const uint64_t result = destinationHandle == 0
            ? destination.handles().add(duplicate, HandleType::Channel, rights)
            : (destination.handles().addAt(destinationHandle, duplicate,
                                           HandleType::Channel, rights)
                   ? destinationHandle : 0);
        if (!result) closeChannelHandleObject(duplicate);
        return result;
    }
    auto* original     = static_cast<PipeEnd*>(object);
    PipeEnd* duplicate = nullptr;
    {
        InterruptSpinLockGuard guard(original->pipe->lock);
        if (original->closed) {
            return 0;
        }
        duplicate = createEnd(original->pipe, original->readable);
        if (duplicate == nullptr) {
            return 0;
        }
        if (duplicate->readable) {
            ++duplicate->pipe->readers;
        } else {
            ++duplicate->pipe->writers;
        }
    }
    const uint64_t result =
        destinationHandle == 0
            ? destination.handles().add(duplicate, HandleType::Pipe, rights)
            : (destination.handles().addAt(destinationHandle, duplicate,
                                           HandleType::Pipe, rights)
                   ? destinationHandle
                   : 0);
    if (result == 0) {
        Pipe::closeHandleObject(duplicate);
    }
    return result;
}

static int64_t readEnd(PipeEnd* end, void* buffer, size_t size,
                       bool nonblocking) {
    if (end == nullptr || buffer == nullptr || size == 0 || !end->readable) {
        return -2;
    }
    PipeState* pipe = end->pipe;
    while (true) {
        {
            InterruptSpinLockGuard guard(pipe->lock);
            if (end->closed) {
                return -2;
            }
            if (pipe->size != 0) {
                size_t count = size < pipe->size ? size : pipe->size;
                auto* target = static_cast<uint8_t*>(buffer);
                for (size_t index = 0; index < count; ++index) {
                    target[index] = pipe->buffer[pipe->head];
                    pipe->head    = (pipe->head + 1) % pipeCapacity;
                }
                pipe->size -= static_cast<uint32_t>(count);
                wakeOne(pipe->writerWaiters);
                return static_cast<int64_t>(count);
            }
            if (pipe->eofEvents != 0) {
                --pipe->eofEvents;
                return 0;
            }
            if (pipe->writers == 0) {
                return 0;
            }
            if (nonblocking) return -6;
            addWaiter(pipe->readerWaiters, Thread::getCurrent());
        }
        Thread::blockCurrent();
    }
}

static int64_t writeEnd(PipeEnd* end, const void* buffer, size_t size,
                        bool nonblocking) {
    if (end == nullptr || buffer == nullptr || size == 0 || end->readable) {
        return -2;
    }
    PipeState* pipe = end->pipe;
    while (true) {
        {
            InterruptSpinLockGuard guard(pipe->lock);
            if (end->closed) {
                return -2;
            }
            if (pipe->readers == 0) {
                return -11;
            }
            if (pipe->size != pipeCapacity) {
                const uint32_t available = pipeCapacity - pipe->size;
                const size_t count       = size < available ? size : available;
                const auto* source       = static_cast<const uint8_t*>(buffer);
                for (size_t index = 0; index < count; ++index) {
                    pipe->buffer[pipe->tail] = source[index];
                    pipe->tail               = (pipe->tail + 1) % pipeCapacity;
                }
                pipe->size += static_cast<uint32_t>(count);
                wakeOne(pipe->readerWaiters);
                return static_cast<int64_t>(count);
            }
            if (nonblocking) return -6;
            addWaiter(pipe->writerWaiters, Thread::getCurrent());
        }
        Thread::blockCurrent();
    }
}

int64_t Pipe::read(Task& task, uint64_t handle, void* buffer, size_t size) {
    void* object = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!task.handles().inspect(handle, &object, &type, &rights) ||
        (rights & handleRightRead) == 0) return -2;
    if (type == HandleType::Channel) {
        auto* channel = static_cast<ChannelEnd*>(object);
        return channel->closed ? -2 : readEnd(channel->input, buffer, size,
            __atomic_load_n(channel->nonblocking, __ATOMIC_ACQUIRE));
    }
    return type == HandleType::Pipe
               ? readEnd(static_cast<PipeEnd*>(object), buffer, size, false) : -2;
}

int64_t Pipe::write(Task& task, uint64_t handle, const void* buffer,
                    size_t size) {
    void* object = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!task.handles().inspect(handle, &object, &type, &rights) ||
        (rights & handleRightWrite) == 0) return -2;
    if (type == HandleType::Channel) {
        auto* channel = static_cast<ChannelEnd*>(object);
        return channel->closed ? -2 : writeEnd(channel->output, buffer, size,
            __atomic_load_n(channel->nonblocking, __ATOMIC_ACQUIRE));
    }
    return type == HandleType::Pipe
               ? writeEnd(static_cast<PipeEnd*>(object), buffer, size, false) : -2;
}

void Pipe::closeHandleObject(void* object) {
    auto* end = static_cast<PipeEnd*>(object);
    if (end == nullptr || end->pipe == nullptr) {
        return;
    }
    PipeState* pipe = end->pipe;
    InterruptSpinLockGuard guard(pipe->lock);
    if (end->closed) {
        return;
    }
    end->closed = true;
    if (end->readable) {
        if (pipe->readers != 0) {
            --pipe->readers;
        }
        wakeAll(pipe->writerWaiters);
    } else {
        if (pipe->writers != 0) {
            --pipe->writers;
        }
        wakeAll(pipe->readerWaiters);
    }
}

void Pipe::closeChannelHandleObject(void* object) {
    auto* end = static_cast<ChannelEnd*>(object);
    if (end == nullptr || end->closed) return;
    end->closed = true;
    closeHandleObject(end->input);
    closeHandleObject(end->output);
}

}  // namespace kernel
