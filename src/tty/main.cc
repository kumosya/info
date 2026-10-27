#include <csignal>

#include "service/common/channel.h"
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/tty/records.h"
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/pty.h>
#include <os/service.h>
#include <os/sync.h>
#include <os/task.h>

namespace {
class TtyServer {
   public:
    static constexpr uint32_t ptyCapacity  = 8;
    static constexpr uint32_t lineCapacity = 4096;
    static constexpr uint64_t stackBase    = 0x70000000ULL;
    static constexpr uint64_t stackSize    = 64 * 1024;

    struct PtyState {
        os::SpinLock lock;
        uint64_t masterServer;
        uint64_t slaveServer;
        uint64_t masterIdentity;
        uint64_t slaveIdentity;
        uint64_t foregroundGroup;
        os::wire::TerminalAttributes attributes;
        os::wire::TerminalWindowSize windowSize;
        uint8_t line[lineCapacity];
        uint32_t lineLength;
        bool active;
        uint32_t closing;
        uint64_t workers[2];
    };

    PtyState states[ptyCapacity];

    static void copyRaw(void* destination, const void* source, size_t count) {
        auto* target      = static_cast<uint8_t*>(destination);
        const auto* bytes = static_cast<const uint8_t*>(source);
        for (size_t index = 0; index < count; ++index)
            target[index] = bytes[index];
    }

    static void writeAll(PtyState& state, uint64_t handle, const void* data,
                         size_t count) {
        size_t offset = 0;
        while (offset < count &&
               !__atomic_load_n(&state.closing, __ATOMIC_ACQUIRE)) {
            const auto written = os::detail::PipeClient(handle).write(
                static_cast<const uint8_t*>(data) + offset, count - offset);
            if (written > 0)
                offset += written;
            else if (written == static_cast<int>(os::ErrorCode::WouldBlock))
                os::sleep(1);
            else {
                __atomic_store_n(&state.closing, 1, __ATOMIC_RELEASE);
                break;
            }
        }
    }

    static void masterInput(void* argument) {
        auto& state = *static_cast<PtyState*>(argument);
        uint8_t input[512];
        while (!__atomic_load_n(&state.closing, __ATOMIC_ACQUIRE)) {
            const int64_t count = os::detail::PipeClient(state.masterServer)
                                      .read(input, sizeof(input));
            if (count == static_cast<int>(os::ErrorCode::WouldBlock)) {
                os::sleep(1);
                continue;
            }
            if (count <= 0) break;
            for (int64_t index = 0; index < count; ++index) {
                const uint8_t value = input[index];
                uint8_t echo[3]{};
                size_t echoLength = 0;
                uint8_t delivery[lineCapacity + 1];
                size_t deliveryLength = 0;
                bool endOfFile        = false;
                uint64_t signalGroup  = 0;
                uint32_t signalNumber = 0;
                {
                    os::SpinLock::Guard guard(state.lock);
                    const auto& attributes = state.attributes;
                    if (attributes.signalMode &&
                        (value == attributes.interruptCharacter ||
                         value == attributes.suspendCharacter ||
                         value == attributes.quitCharacter)) {
                        state.lineLength = 0;
                        signalGroup      = state.foregroundGroup;
                        signalNumber =
                            value == attributes.interruptCharacter ? SIGINT
                            : value == attributes.suspendCharacter ? SIGTSTP
                                                                   : SIGQUIT;
                        if (attributes.echo) {
                            echo[0]    = '^';
                            echo[1]    = value + '@';
                            echo[2]    = '\n';
                            echoLength = 3;
                        }
                    } else if (attributes.canonicalMode &&
                               value == attributes.eraseCharacter) {
                        if (state.lineLength) {
                            --state.lineLength;
                            if (attributes.echo) {
                                echo[0]    = '\b';
                                echo[1]    = ' ';
                                echo[2]    = '\b';
                                echoLength = 3;
                            }
                        }
                    } else if (attributes.canonicalMode &&
                               value == attributes.eofCharacter) {
                        deliveryLength = state.lineLength;
                        copyRaw(delivery, state.line, deliveryLength);
                        state.lineLength = 0;
                        endOfFile        = deliveryLength == 0;
                    } else {
                        if (attributes.echo) {
                            echo[0]    = value;
                            echoLength = 1;
                        }
                        if (!attributes.canonicalMode) {
                            delivery[0]    = value;
                            deliveryLength = 1;
                        } else if (state.lineLength < lineCapacity) {
                            state.line[state.lineLength++] = value;
                            if (value == '\n' ||
                                state.lineLength == lineCapacity) {
                                deliveryLength = state.lineLength;
                                copyRaw(delivery, state.line, deliveryLength);
                                state.lineLength = 0;
                            }
                        }
                    }
                }
                writeAll(state, state.masterServer, echo, echoLength);
                writeAll(state, state.slaveServer, delivery, deliveryLength);
                if (endOfFile)
                    os::detail::PipeClient(state.slaveServer).signalEof();
                if (signalGroup)
                    os::detail::TaskControl().sendSignal(
                        static_cast<uint64_t>(
                            -static_cast<int64_t>(signalGroup)),
                        signalNumber);
            }
        }
        __atomic_store_n(&state.closing, 1, __ATOMIC_RELEASE);
        os::detail::PipeClient(state.slaveServer).signalEof();
        os::exitThread(0);
    }

    static void slaveOutput(void* argument) {
        auto& state = *static_cast<PtyState*>(argument);
        uint8_t bytes[512];
        while (!__atomic_load_n(&state.closing, __ATOMIC_ACQUIRE)) {
            const int64_t count = os::detail::PipeClient(state.slaveServer)
                                      .read(bytes, sizeof(bytes));
            if (count == static_cast<int>(os::ErrorCode::WouldBlock)) {
                os::sleep(1);
                continue;
            }
            if (count <= 0) break;
            writeAll(state, state.masterServer, bytes,
                     static_cast<size_t>(count));
        }
        __atomic_store_n(&state.closing, 1, __ATOMIC_RELEASE);
        os::detail::PipeClient(state.masterServer).signalEof();
        os::exitThread(0);
    }

    PtyState* findState(uint64_t identity) {
        for (uint32_t index = 0; index < ptyCapacity; ++index) {
            PtyState& state = states[index];
            if (state.active &&
                !__atomic_load_n(&state.closing, __ATOMIC_ACQUIRE) &&
                (state.masterIdentity == identity ||
                 state.slaveIdentity == identity))
                return &state;
        }
        return nullptr;
    }

    uint64_t startWorker(void (*entry)(void*), PtyState& state, uint32_t slot) {
        const uint64_t base =
            stackBase + static_cast<uint64_t>(slot) * stackSize;
        if (os::mapMemory(base, stackSize, 1) != base) return 0;
        const auto thread = os::createThread(entry, base + stackSize, &state);
        if (!thread) os::unmapMemory(base, stackSize);
        return thread;
    }

    void reap() {
        for (uint32_t index = 0; index < ptyCapacity; ++index) {
            auto& state = states[index];
            if (!state.active ||
                !__atomic_load_n(&state.closing, __ATOMIC_ACQUIRE))
                continue;
            for (uint32_t n = 0; n < 2; ++n)
                if (state.workers[n]) {
                    int code = 0;
                    if (os::detail::TaskControl().waitThread(state.workers[n],
                                                             code, true) == 0) {
                        state.workers[n] = 0;
                        os::unmapMemory(stackBase + (index * 2 + n) * stackSize,
                                        stackSize);
                    }
                }
            if (!state.workers[0] && !state.workers[1]) {
                os::closeHandle(state.masterServer);
                os::closeHandle(state.slaveServer);
                state.active = false;
            }
        }
    }

    bool createPty(os::IpcMessage& reply, uint64_t& masterClient,
                   uint64_t& slaveClient) {
        reap();
        uint32_t index = 0;
        while (index < ptyCapacity && states[index].active) ++index;
        if (index == ptyCapacity) return false;
        os::Ipc::Pipe master, slave;
        if (master.create(true) != 0) return false;
        if (slave.create(true) != 0) {
            return false;
        }
        PtyState& state    = states[index];
        state.masterServer = master.releaseReadHandle();
        state.slaveServer  = slave.releaseReadHandle();
        state.masterIdentity =
            os::detail::PipeClient(master.getWriteHandle()).getIdentity();
        state.slaveIdentity =
            os::detail::PipeClient(slave.getWriteHandle()).getIdentity();
        state.attributes      = {true, true, true, 0x7f, 3, 4, 26, 28};
        state.windowSize      = {25, 80, 0, 0};
        state.lineLength      = 0;
        state.foregroundGroup = 0;
        state.active          = true;
        __atomic_store_n(&state.closing, 0, __ATOMIC_RELEASE);
        state.workers[0] = state.workers[1] = 0;
        if (!os::detail::PipeClient(state.masterServer).setNonblocking(true) ||
            !os::detail::PipeClient(state.slaveServer).setNonblocking(true) ||
            !(state.workers[0] = startWorker(masterInput, state, index * 2)) ||
            !(state.workers[1] =
                  startWorker(slaveOutput, state, index * 2 + 1))) {
            __atomic_store_n(&state.closing, 1, __ATOMIC_RELEASE);

            return false;
        }
        masterClient      = master.releaseWriteHandle();
        slaveClient       = slave.releaseWriteHandle();
        reply.handleCount = 2;
        reply.handles[0]  = masterClient;
        reply.handles[1]  = slaveClient;
        return true;
    }

    void handleControl(const os::IpcMessage& request, os::IpcMessage& reply) {
        reply.length  = 1;
        reply.data[0] = 1;
        if (request.handleCount != 1) return;
        const uint64_t handle = request.handles[0];
        PtyState* state =
            findState(os::detail::PipeClient(handle).getIdentity());
        if (!state) {
            os::closeHandle(handle);
            return;
        }
        const auto operation = static_cast<os::TtyRequest>(request.type);
        switch (operation) {
            case os::TtyRequest::GetForegroundGroup: {
                os::SpinLock::Guard guard(state->lock);
                reply.data[0] = 0;
                reply.data[1] = state->foregroundGroup;
                reply.length  = 2;
                break;
            }
            case os::TtyRequest::GetNumber:
                reply.data[0] = 0;
                reply.data[1] = static_cast<uint64_t>(state - states);
                reply.length  = 2;
                break;
            case os::TtyRequest::GetAttributes: {
                os::SpinLock::Guard guard(state->lock);
                copyRaw(&reply.data[1], &state->attributes,
                        sizeof(state->attributes));
                reply.length  = 1 + (sizeof(state->attributes) + 7) / 8;
                reply.data[0] = 0;
                break;
            }
            case os::TtyRequest::SetAttributes:
                if (request.length * 8 >= sizeof(state->attributes)) {
                    os::SpinLock::Guard guard(state->lock);
                    copyRaw(&state->attributes, request.data,
                            sizeof(state->attributes));
                    reply.data[0] = 0;
                }
                break;
            case os::TtyRequest::GetWindowSize: {
                os::SpinLock::Guard guard(state->lock);
                copyRaw(&reply.data[1], &state->windowSize,
                        sizeof(state->windowSize));
                reply.length  = 1 + (sizeof(state->windowSize) + 7) / 8;
                reply.data[0] = 0;
                break;
            }
            case os::TtyRequest::SetWindowSize:
                if (request.length * 8 >= sizeof(state->windowSize)) {
                    os::SpinLock::Guard guard(state->lock);
                    copyRaw(&state->windowSize, request.data,
                            sizeof(state->windowSize));
                    reply.data[0] = 0;
                }
                break;
            case os::TtyRequest::SetForegroundGroup:
                if (request.length >= 1) {
                    os::SpinLock::Guard guard(state->lock);
                    state->foregroundGroup = request.data[0];
                    os::IpcMessage metadata{}, result{};
                    metadata.type = static_cast<uint64_t>(
                        os::TaskOperation::SetTerminalForeground);
                    metadata.length  = 4;
                    metadata.data[0] = 2;
                    metadata.data[1] = state - states;
                    metadata.data[2] = state->foregroundGroup;
                    reply.data[0] =
                        services::RequestChannel(os::CoreServiceKind::Task)
                            .result(metadata, result);
                }
                break;
            case os::TtyRequest::SetNonblocking:
                if (request.length >= 1 &&
                    os::detail::PipeClient(handle).setNonblocking(
                        request.data[0] != 0))
                    reply.data[0] = 0;
                break;
            default:
                break;
        }
        os::closeHandle(handle);
    }

    int run() {
        const uint64_t registry = os::getBootstrapHandle();
        const uint64_t endpoint = os::detail::EndpointControl().create();
        if (!registry || !endpoint ||
            !os::detail::RegistryClient(registry).registerName(
                os::ttyServiceName, endpoint))
            return 1;
        os::Sys().log("[TTY] PTY service started\n");
        while (true) {
            reap();
            os::IpcMessage request{};
            if (os::receiveMessage(endpoint, request,
                                   os::Sys().readMonotonicTime() + 100000000) !=
                0)
                continue;
            os::IpcMessage reply{};
            uint64_t masterClient = 0, slaveClient = 0;
            if (request.type ==
                static_cast<uint64_t>(os::TtyRequest::CreatePty)) {
                reply.data[0] =
                    createPty(reply, masterClient, slaveClient) ? 0 : 1;
                reply.length = 1;
            } else
                handleControl(request, reply);
            os::replyMessage(endpoint, request.replyToken, reply);
            if (masterClient) os::closeHandle(masterClient);
            if (slaveClient) os::closeHandle(slaveClient);
        }
    }
};
}  // namespace

int main(int, char**) {
    static TtyServer server;
    return server.run();
}
