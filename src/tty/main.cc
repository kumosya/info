#include <signal.h>

#include "os/abi.h"
#include "os/protocol.h"
#include "os/pty.h"
#include "os/service.h"
#include "os/sync.h"

namespace {

constexpr uint32_t ptyCapacity = 8;
constexpr uint32_t lineCapacity = 4096;
constexpr uint64_t stackBase = 0x70000000ULL;
constexpr uint64_t stackSize = 64 * 1024;

struct PtyState {
    os::SpinLock lock;
    uint64_t masterServer;
    uint64_t slaveServer;
    uint64_t masterIdentity;
    uint64_t slaveIdentity;
    uint64_t foregroundGroup;
    os::TerminalAttributes attributes;
    os::TerminalWindowSize windowSize;
    uint8_t line[lineCapacity];
    uint32_t lineLength;
    bool active;
};

PtyState states[ptyCapacity];
uint32_t stateCount;

void copyRaw(void* destination, const void* source, size_t count) {
    auto* target = static_cast<uint8_t*>(destination);
    const auto* bytes = static_cast<const uint8_t*>(source);
    for (size_t index = 0; index < count; ++index) target[index] = bytes[index];
}

void writeAll(uint64_t handle, const void* data, size_t count) {
    if (count) os::writePipe(handle, data, count);
}

void masterInput(void* argument) {
    auto& state = *static_cast<PtyState*>(argument);
    uint8_t input[512];
    while (true) {
        const int64_t count = os::readPipe(state.masterServer, input,
                                            sizeof(input));
        if (count <= 0) break;
        for (int64_t index = 0; index < count; ++index) {
            const uint8_t value = input[index];
            uint8_t echo[3]{};
            size_t echoLength = 0;
            uint8_t delivery[lineCapacity + 1];
            size_t deliveryLength = 0;
            bool endOfFile = false;
            uint64_t signalGroup = 0;
            uint32_t signalNumber = 0;
            {
                os::SpinLockGuard guard(state.lock);
                const auto& attributes = state.attributes;
                if (attributes.signalMode &&
                    (value == attributes.interruptCharacter ||
                     value == attributes.suspendCharacter ||
                     value == attributes.quitCharacter)) {
                    state.lineLength = 0;
                    signalGroup = state.foregroundGroup;
                    signalNumber = value == attributes.interruptCharacter
                        ? SIGINT : value == attributes.suspendCharacter
                            ? SIGTSTP : SIGQUIT;
                    if (attributes.echo) {
                        echo[0] = '^'; echo[1] = value + '@'; echo[2] = '\n';
                        echoLength = 3;
                    }
                } else if (attributes.canonicalMode &&
                           value == attributes.eraseCharacter) {
                    if (state.lineLength) {
                        --state.lineLength;
                        if (attributes.echo) {
                            echo[0] = '\b'; echo[1] = ' '; echo[2] = '\b';
                            echoLength = 3;
                        }
                    }
                } else if (attributes.canonicalMode &&
                           value == attributes.eofCharacter) {
                    deliveryLength = state.lineLength;
                    copyRaw(delivery, state.line, deliveryLength);
                    state.lineLength = 0;
                    endOfFile = deliveryLength == 0;
                } else {
                    if (attributes.echo) {
                        echo[0] = value;
                        echoLength = 1;
                    }
                    if (!attributes.canonicalMode) {
                        delivery[0] = value;
                        deliveryLength = 1;
                    } else if (state.lineLength < lineCapacity) {
                        state.line[state.lineLength++] = value;
                        if (value == '\n' || state.lineLength == lineCapacity) {
                            deliveryLength = state.lineLength;
                            copyRaw(delivery, state.line, deliveryLength);
                            state.lineLength = 0;
                        }
                    }
                }
            }
            writeAll(state.masterServer, echo, echoLength);
            writeAll(state.slaveServer, delivery, deliveryLength);
            if (endOfFile) os::signalChannelEof(state.slaveServer);
            if (signalGroup) os::sendSignal(static_cast<uint64_t>(
                -static_cast<int64_t>(signalGroup)), signalNumber);
        }
    }
}

void slaveOutput(void* argument) {
    auto& state = *static_cast<PtyState*>(argument);
    uint8_t bytes[512];
    while (true) {
        const int64_t count = os::readPipe(state.slaveServer, bytes,
                                            sizeof(bytes));
        if (count <= 0) break;
        writeAll(state.masterServer, bytes, static_cast<size_t>(count));
    }
}

PtyState* findState(uint64_t identity) {
    for (uint32_t index = 0; index < stateCount; ++index) {
        PtyState& state = states[index];
        if (state.active && (state.masterIdentity == identity ||
                             state.slaveIdentity == identity)) return &state;
    }
    return nullptr;
}

bool startWorker(void (*entry)(void*), PtyState& state, uint32_t slot) {
    const uint64_t base = stackBase + static_cast<uint64_t>(slot) * stackSize;
    return os::mapMemory(base, stackSize, 1) == base &&
           os::createThread(entry, base + stackSize, &state) != 0;
}

bool createPty(os::IpcMessage& reply, uint64_t& masterClient,
               uint64_t& slaveClient) {
    if (stateCount >= ptyCapacity) return false;
    os::ChannelHandles master{}, slave{};
    if (!os::createChannelPair(master)) return false;
    if (!os::createChannelPair(slave)) {
        os::closeHandle(master.firstHandle);
        os::closeHandle(master.secondHandle);
        return false;
    }
    PtyState& state = states[stateCount];
    state.masterServer = master.firstHandle;
    state.slaveServer = slave.firstHandle;
    state.masterIdentity = os::getChannelIdentity(master.secondHandle);
    state.slaveIdentity = os::getChannelIdentity(slave.secondHandle);
    state.attributes = {true, true, true, 0x7f, 3, 4, 26, 28};
    state.windowSize = {25, 80, 0, 0};
    state.lineLength = 0;
    state.foregroundGroup = 0;
    state.active = true;
    const uint32_t slot = stateCount++ * 2;
    if (!startWorker(masterInput, state, slot) ||
        !startWorker(slaveOutput, state, slot + 1)) return false;
    masterClient = master.secondHandle;
    slaveClient = slave.secondHandle;
    reply.handleCount = 2;
    reply.handles[0] = masterClient;
    reply.handles[1] = slaveClient;
    return true;
}

void handleControl(const os::IpcMessage& request, os::IpcMessage& reply) {
    reply.length = 1;
    reply.data[0] = 1;
    if (request.handleCount != 1) return;
    const uint64_t handle = request.handles[0];
    PtyState* state = findState(os::getChannelIdentity(handle));
    if (!state) {
        os::closeHandle(handle);
        return;
    }
    const auto operation = static_cast<os::TtyRequest>(request.type);
    switch (operation) {
        case os::TtyRequest::GetAttributes: {
            os::SpinLockGuard guard(state->lock);
            copyRaw(&reply.data[1], &state->attributes,
                      sizeof(state->attributes));
            reply.length = 1 + (sizeof(state->attributes) + 7) / 8;
            reply.data[0] = 0;
            break;
        }
        case os::TtyRequest::SetAttributes:
            if (request.length * 8 >= sizeof(state->attributes)) {
                os::SpinLockGuard guard(state->lock);
                copyRaw(&state->attributes, request.data,
                          sizeof(state->attributes));
                reply.data[0] = 0;
            }
            break;
        case os::TtyRequest::GetWindowSize: {
            os::SpinLockGuard guard(state->lock);
            copyRaw(&reply.data[1], &state->windowSize,
                      sizeof(state->windowSize));
            reply.length = 1 + (sizeof(state->windowSize) + 7) / 8;
            reply.data[0] = 0;
            break;
        }
        case os::TtyRequest::SetWindowSize:
            if (request.length * 8 >= sizeof(state->windowSize)) {
                os::SpinLockGuard guard(state->lock);
                copyRaw(&state->windowSize, request.data,
                          sizeof(state->windowSize));
                reply.data[0] = 0;
            }
            break;
        case os::TtyRequest::SetForegroundGroup:
            if (request.length >= 1) {
                os::SpinLockGuard guard(state->lock);
                state->foregroundGroup = request.data[0];
                reply.data[0] = 0;
            }
            break;
        case os::TtyRequest::SetNonblocking:
            if (request.length >= 1 &&
                os::setChannelNonblocking(handle, request.data[0] != 0))
                reply.data[0] = 0;
            break;
        default:
            break;
    }
    os::closeHandle(handle);
}

}  // namespace

int main(int, char**) {
    const uint64_t registry = os::getBootstrapHandle();
    const uint64_t endpoint = os::createEndpoint();
    if (!registry || !endpoint ||
        !os::registerService(registry, os::ttyServiceName, endpoint)) return 1;
    os::log("[TTY] PTY service started\n");
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(endpoint, request) != 0) continue;
        os::IpcMessage reply{};
        uint64_t masterClient = 0, slaveClient = 0;
        if (request.type == static_cast<uint64_t>(os::TtyRequest::CreatePty)) {
            reply.data[0] = createPty(reply, masterClient, slaveClient) ? 0 : 1;
            reply.length = 1;
        } else handleControl(request, reply);
        os::replyMessage(endpoint, reply);
        if (masterClient) os::closeHandle(masterClient);
        if (slaveClient) os::closeHandle(slaveClient);
    }
}
