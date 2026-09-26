#include "kernel/pty.h"

#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/signal.h"
#include "kernel/spinlock.h"
#include "kernel/thread.h"

namespace kernel {

namespace {

constexpr uint32_t bufferCapacity = 16 * 1024;
constexpr uint32_t lineCapacity = 4096;

struct RingBuffer {
    uint8_t bytes[bufferCapacity];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
};

struct Pty {
    SpinLock lock;
    RingBuffer input;
    RingBuffer output;
    uint8_t line[lineCapacity];
    uint32_t lineLength;
    uint32_t masterReferences;
    uint32_t slaveReferences;
    uint64_t foregroundProcessGroup;
    bool endOfFile;
    os::TerminalAttributes attributes;
    os::TerminalWindowSize windowSize;
    Thread* inputReaders;
    Thread* inputWriters;
    Thread* outputReaders;
    Thread* outputWriters;
};

struct PtyEnd {
    Pty* pty;
    bool master;
    bool nonblocking;
    bool closed;
};

void clearMemory(void* address, size_t size) {
    auto* bytes = static_cast<uint8_t*>(address);
    for (size_t index = 0; index < size; ++index) bytes[index] = 0;
}

void addWaiter(Thread*& waiters, Thread* thread) {
    thread->waitNext = waiters;
    waiters = thread;
}

void wakeOne(Thread*& waiters) {
    if (waiters == nullptr) return;
    Thread* thread = waiters;
    waiters = thread->waitNext;
    thread->waitNext = nullptr;
    wakeThread(thread);
}

void wakeAll(Thread*& waiters) {
    while (waiters != nullptr) wakeOne(waiters);
}

bool pushByte(RingBuffer& buffer, uint8_t value) {
    if (buffer.count == bufferCapacity) return false;
    buffer.bytes[buffer.tail] = value;
    buffer.tail = (buffer.tail + 1) % bufferCapacity;
    ++buffer.count;
    return true;
}

uint8_t popByte(RingBuffer& buffer) {
    const uint8_t value = buffer.bytes[buffer.head];
    buffer.head = (buffer.head + 1) % bufferCapacity;
    --buffer.count;
    return value;
}

void echoByte(Pty& pty, uint8_t value) {
    if (pty.attributes.echo) pushByte(pty.output, value);
}

void echoControl(Pty& pty, uint8_t value) {
    if (!pty.attributes.echo) return;
    pushByte(pty.output, '^');
    pushByte(pty.output, static_cast<uint8_t>(value + '@'));
    pushByte(pty.output, '\n');
}

void flushLine(Pty& pty) {
    for (uint32_t index = 0;
         index < pty.lineLength && pty.input.count < bufferCapacity; ++index) {
        pushByte(pty.input, pty.line[index]);
    }
    pty.lineLength = 0;
    wakeOne(pty.inputReaders);
}

PtyEnd* createEnd(Pty* pty, bool master) {
    auto* end = static_cast<PtyEnd*>(kernelAllocate(sizeof(PtyEnd)));
    if (end != nullptr) *end = {pty, master, false, false};
    return end;
}

PtyEnd* resolve(Process& process, uint64_t handle, uint32_t rights) {
    return static_cast<PtyEnd*>(
        process.handles().get(handle, HandleType::Pty, rights));
}

}  // namespace

bool createPtyHandles(Process& process, uint64_t& masterHandle,
                      uint64_t& slaveHandle) {
    masterHandle = 0;
    slaveHandle = 0;
    auto* pty = static_cast<Pty*>(kernelAllocate(sizeof(Pty)));
    if (pty == nullptr) return false;
    clearMemory(pty, sizeof(Pty));
    pty->lock.initialize();
    pty->masterReferences = 1;
    pty->slaveReferences = 1;
    pty->attributes = {true, true, true, 0x7f, 0x03, 0x04, 0x1a, 0x1c};
    pty->windowSize = {25, 80, 640, 400};
    PtyEnd* master = createEnd(pty, true);
    PtyEnd* slave = createEnd(pty, false);
    if (master == nullptr || slave == nullptr) return false;
    masterHandle = process.handles().add(
        master, HandleType::Pty, handleRightRead | handleRightWrite |
                                     handleRightManage);
    slaveHandle = process.handles().add(
        slave, HandleType::Pty, handleRightRead | handleRightWrite |
                                    handleRightManage);
    if (masterHandle == 0 || slaveHandle == 0) {
        if (masterHandle != 0) process.handles().close(masterHandle);
        if (slaveHandle != 0) process.handles().close(slaveHandle);
        return false;
    }
    return true;
}

uint64_t duplicatePtyHandle(Process& source, Process& destination,
                            uint64_t handle, uint64_t destinationHandle) {
    void* object = nullptr;
    HandleType type = HandleType::None;
    uint32_t rights = 0;
    if (!source.handles().inspect(handle, &object, &type, &rights) ||
        type != HandleType::Pty) return 0;
    auto* original = static_cast<PtyEnd*>(object);
    PtyEnd* duplicate = nullptr;
    {
        InterruptSpinLockGuard guard(original->pty->lock);
        if (original->closed) return 0;
        duplicate = createEnd(original->pty, original->master);
        if (duplicate == nullptr) return 0;
        duplicate->nonblocking = original->nonblocking;
        if (duplicate->master) ++original->pty->masterReferences;
        else ++original->pty->slaveReferences;
    }
    const uint64_t result =
        destinationHandle == 0
            ? destination.handles().add(duplicate, HandleType::Pty, rights)
            : (destination.handles().addAt(destinationHandle, duplicate,
                                           HandleType::Pty, rights)
                   ? destinationHandle
                   : 0);
    if (result == 0) closePtyHandleObject(duplicate);
    return result;
}

int64_t readPtyHandle(Process& process, uint64_t handle, void* buffer,
                      size_t size) {
    PtyEnd* end = resolve(process, handle, handleRightRead);
    if (end == nullptr || buffer == nullptr || size == 0) return -2;
    Pty* pty = end->pty;
    RingBuffer& source = end->master ? pty->output : pty->input;
    Thread*& readers = end->master ? pty->outputReaders : pty->inputReaders;
    Thread*& writers = end->master ? pty->outputWriters : pty->inputWriters;
    while (true) {
        {
            InterruptSpinLockGuard guard(pty->lock);
            if (end->closed) return -2;
            if (!end->master && pty->endOfFile && source.count == 0) {
                pty->endOfFile = false;
                return 0;
            }
            if (source.count != 0) {
                const size_t count = size < source.count ? size : source.count;
                auto* target = static_cast<uint8_t*>(buffer);
                for (size_t index = 0; index < count; ++index) {
                    target[index] = popByte(source);
                }
                wakeOne(writers);
                return static_cast<int64_t>(count);
            }
            const bool peerClosed = end->master ? pty->slaveReferences == 0
                                                : pty->masterReferences == 0;
            if (peerClosed) return 0;
            if (end->nonblocking) return -6;
            addWaiter(readers, getCurrentThread());
        }
        blockThread();
    }
}

int64_t writePtyHandle(Process& process, uint64_t handle, const void* buffer,
                       size_t size) {
    PtyEnd* end = resolve(process, handle, handleRightWrite);
    if (end == nullptr || buffer == nullptr || size == 0) return -2;
    Pty* pty = end->pty;
    const auto* source = static_cast<const uint8_t*>(buffer);
    if (!end->master) {
        while (true) {
            {
                InterruptSpinLockGuard guard(pty->lock);
                if (end->closed || pty->masterReferences == 0) return -11;
                if (pty->output.count != bufferCapacity) {
                    const uint32_t available = bufferCapacity - pty->output.count;
                    const size_t count = size < available ? size : available;
                    for (size_t index = 0; index < count; ++index) {
                        pushByte(pty->output, source[index]);
                    }
                    wakeOne(pty->outputReaders);
                    return static_cast<int64_t>(count);
                }
                if (end->nonblocking) return -6;
                addWaiter(pty->outputWriters, getCurrentThread());
            }
            blockThread();
        }
    }

    size_t consumed = 0;
    while (consumed < size) {
        uint32_t signal = 0;
        uint64_t processGroup = 0;
        bool shouldBlock = false;
        {
            InterruptSpinLockGuard guard(pty->lock);
            if (end->closed || pty->slaveReferences == 0) return -11;
            const uint8_t value = source[consumed];
            if (pty->attributes.signalMode &&
                (value == pty->attributes.interruptCharacter ||
                 value == pty->attributes.suspendCharacter ||
                 value == pty->attributes.quitCharacter)) {
                signal = value == pty->attributes.interruptCharacter
                             ? static_cast<uint32_t>(Signal::Interrupt)
                             : value == pty->attributes.suspendCharacter
                                   ? static_cast<uint32_t>(Signal::TerminalStop)
                                   : static_cast<uint32_t>(Signal::Quit);
                processGroup = pty->foregroundProcessGroup;
                pty->lineLength = 0;
                echoControl(*pty, value);
                wakeOne(pty->outputReaders);
                ++consumed;
            } else if (pty->attributes.canonicalMode) {
                if (value == pty->attributes.eraseCharacter || value == 8) {
                    if (pty->lineLength != 0) {
                        --pty->lineLength;
                        if (pty->attributes.echo) {
                            echoByte(*pty, 8);
                            echoByte(*pty, ' ');
                            echoByte(*pty, 8);
                        }
                    }
                    ++consumed;
                } else if (value == pty->attributes.eofCharacter) {
                    if (pty->lineLength != 0) flushLine(*pty);
                    else pty->endOfFile = true;
                    wakeOne(pty->inputReaders);
                    ++consumed;
                } else if (pty->lineLength < lineCapacity &&
                           pty->input.count + pty->lineLength < bufferCapacity) {
                    pty->line[pty->lineLength++] = value;
                    echoByte(*pty, value);
                    wakeOne(pty->outputReaders);
                    if (value == '\n' || value == '\r') flushLine(*pty);
                    ++consumed;
                } else {
                    shouldBlock = true;
                }
            } else if (pushByte(pty->input, value)) {
                echoByte(*pty, value);
                wakeOne(pty->inputReaders);
                wakeOne(pty->outputReaders);
                ++consumed;
            } else {
                shouldBlock = true;
            }
            if (shouldBlock) {
                if (end->nonblocking) return consumed == 0 ? -6 : consumed;
                addWaiter(pty->inputWriters, getCurrentThread());
            }
        }
        if (signal != 0 && processGroup != 0) {
            sendSignalToProcessGroup(processGroup, signal);
        }
        if (shouldBlock) blockThread();
    }
    return static_cast<int64_t>(consumed);
}

bool getPtyAttributes(Process& process, uint64_t handle,
                      os::TerminalAttributes& attributes) {
    PtyEnd* end = resolve(process, handle, 0);
    if (end == nullptr) return false;
    InterruptSpinLockGuard guard(end->pty->lock);
    attributes = end->pty->attributes;
    return true;
}

bool setPtyAttributes(Process& process, uint64_t handle,
                      const os::TerminalAttributes& attributes) {
    PtyEnd* end = resolve(process, handle, handleRightManage);
    if (end == nullptr) return false;
    InterruptSpinLockGuard guard(end->pty->lock);
    end->pty->attributes = attributes;
    return true;
}

bool getPtyWindowSize(Process& process, uint64_t handle,
                      os::TerminalWindowSize& size) {
    PtyEnd* end = resolve(process, handle, 0);
    if (end == nullptr) return false;
    InterruptSpinLockGuard guard(end->pty->lock);
    size = end->pty->windowSize;
    return true;
}

bool setPtyWindowSize(Process& process, uint64_t handle,
                      const os::TerminalWindowSize& size) {
    PtyEnd* end = resolve(process, handle, handleRightManage);
    if (end == nullptr || size.rows == 0 || size.columns == 0) return false;
    uint64_t processGroup = 0;
    {
        InterruptSpinLockGuard guard(end->pty->lock);
        end->pty->windowSize = size;
        processGroup = end->pty->foregroundProcessGroup;
    }
    if (processGroup != 0) {
        sendSignalToProcessGroup(
            processGroup, static_cast<uint32_t>(Signal::WindowChanged));
    }
    return true;
}

bool setPtyForegroundGroup(Process& process, uint64_t handle,
                           uint64_t processGroupId) {
    PtyEnd* end = resolve(process, handle, handleRightManage);
    if (end == nullptr) return false;
    InterruptSpinLockGuard guard(end->pty->lock);
    end->pty->foregroundProcessGroup = processGroupId;
    return true;
}

bool setPtyNonblocking(Process& process, uint64_t handle, bool enabled) {
    PtyEnd* end = resolve(process, handle, handleRightManage);
    if (end == nullptr) return false;
    InterruptSpinLockGuard guard(end->pty->lock);
    end->nonblocking = enabled;
    return true;
}

void closePtyHandleObject(void* object) {
    auto* end = static_cast<PtyEnd*>(object);
    if (end == nullptr || end->pty == nullptr) return;
    Pty* pty = end->pty;
    InterruptSpinLockGuard guard(pty->lock);
    if (end->closed) return;
    end->closed = true;
    if (end->master) {
        if (pty->masterReferences != 0) --pty->masterReferences;
        wakeAll(pty->inputReaders);
        wakeAll(pty->outputWriters);
    } else {
        if (pty->slaveReferences != 0) --pty->slaveReferences;
        wakeAll(pty->outputReaders);
        wakeAll(pty->inputWriters);
    }
}

}  // namespace kernel
