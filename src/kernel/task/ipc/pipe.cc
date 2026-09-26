#include "kernel/pipe.h"

#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"

namespace kernel {

namespace {

constexpr uint32_t pipeCapacity = 16 * 1024;

struct Pipe {
    SpinLock lock;
    uint8_t buffer[pipeCapacity];
    uint32_t head;
    uint32_t tail;
    uint32_t size;
    uint32_t readers;
    uint32_t writers;
    Thread* readerWaiters;
    Thread* writerWaiters;
};

struct PipeEnd {
    Pipe* pipe;
    bool readable;
    bool closed;
};

void clearMemory(void* address, size_t size) {
    auto* bytes = static_cast<uint8_t*>(address);
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void addWaiter(Thread*& waiters, Thread* thread) {
    thread->waitNext = waiters;
    waiters = thread;
}

void wakeOne(Thread*& waiters) {
    if (waiters == nullptr) {
        return;
    }
    Thread* thread = waiters;
    waiters = thread->waitNext;
    thread->waitNext = nullptr;
    wakeThread(thread);
}

void wakeAll(Thread*& waiters) {
    while (waiters != nullptr) {
        wakeOne(waiters);
    }
}

PipeEnd* createEnd(Pipe* pipe, bool readable) {
    auto* end = static_cast<PipeEnd*>(kernelAllocate(sizeof(PipeEnd)));
    if (end != nullptr) {
        *end = {pipe, readable, false};
    }
    return end;
}

}  // namespace

bool createPipeHandles(Process& process, uint64_t& readHandle,
                       uint64_t& writeHandle) {
    readHandle = 0;
    writeHandle = 0;
    auto* pipe = static_cast<Pipe*>(kernelAllocate(sizeof(Pipe)));
    if (pipe == nullptr) {
        return false;
    }
    clearMemory(pipe, sizeof(Pipe));
    pipe->lock.initialize();
    pipe->readers = 1;
    pipe->writers = 1;
    PipeEnd* readEnd = createEnd(pipe, true);
    PipeEnd* writeEnd = createEnd(pipe, false);
    if (readEnd == nullptr || writeEnd == nullptr) {
        return false;
    }
    readHandle = process.handles().add(readEnd, HandleType::Pipe,
                                       handleRightRead);
    writeHandle = process.handles().add(writeEnd, HandleType::Pipe,
                                        handleRightWrite);
    if (readHandle == 0 || writeHandle == 0) {
        if (readHandle != 0) {
            process.handles().close(readHandle);
        } else {
            closePipeHandleObject(readEnd);
        }
        if (writeHandle != 0) {
            process.handles().close(writeHandle);
        } else {
            closePipeHandleObject(writeEnd);
        }
        readHandle = 0;
        writeHandle = 0;
        return false;
    }
    return true;
}

uint64_t duplicatePipeHandle(Process& source, Process& destination,
                             uint64_t handle, uint64_t destinationHandle) {
    void* object = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!source.handles().inspect(handle, &object, &type, &rights) ||
        type != HandleType::Pipe) {
        return 0;
    }
    auto* original = static_cast<PipeEnd*>(object);
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
        closePipeHandleObject(duplicate);
    }
    return result;
}

int64_t readPipeHandle(Process& process, uint64_t handle, void* buffer,
                       size_t size) {
    auto* end = static_cast<PipeEnd*>(
        process.handles().get(handle, HandleType::Pipe, handleRightRead));
    if (end == nullptr || buffer == nullptr || size == 0 || !end->readable) {
        return -2;
    }
    Pipe* pipe = end->pipe;
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
                    pipe->head = (pipe->head + 1) % pipeCapacity;
                }
                pipe->size -= static_cast<uint32_t>(count);
                wakeOne(pipe->writerWaiters);
                return static_cast<int64_t>(count);
            }
            if (pipe->writers == 0) {
                return 0;
            }
            addWaiter(pipe->readerWaiters, getCurrentThread());
        }
        blockThread();
    }
}

int64_t writePipeHandle(Process& process, uint64_t handle, const void* buffer,
                        size_t size) {
    auto* end = static_cast<PipeEnd*>(
        process.handles().get(handle, HandleType::Pipe, handleRightWrite));
    if (end == nullptr || buffer == nullptr || size == 0 || end->readable) {
        return -2;
    }
    Pipe* pipe = end->pipe;
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
                const size_t count = size < available ? size : available;
                const auto* source = static_cast<const uint8_t*>(buffer);
                for (size_t index = 0; index < count; ++index) {
                    pipe->buffer[pipe->tail] = source[index];
                    pipe->tail = (pipe->tail + 1) % pipeCapacity;
                }
                pipe->size += static_cast<uint32_t>(count);
                wakeOne(pipe->readerWaiters);
                return static_cast<int64_t>(count);
            }
            addWaiter(pipe->writerWaiters, getCurrentThread());
        }
        blockThread();
    }
}

void closePipeHandleObject(void* object) {
    auto* end = static_cast<PipeEnd*>(object);
    if (end == nullptr || end->pipe == nullptr) {
        return;
    }
    Pipe* pipe = end->pipe;
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

}  // namespace kernel
