#pragma once

#include <cstddef>
#include <cstdint>

#include <os/ipc.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/pty.h>
#include <os/service.h>
#include <os/task.h>

namespace kernel {

class Task;
class Thread;
class EndpointService;
struct EndpointImplementation;

constexpr uint32_t endpointRightSend    = 1U << 0;
constexpr uint32_t endpointRightReceive = 1U << 1;
constexpr uint32_t endpointRightManage  = 1U << 2;

using IpcMessage = os::IpcMessage;

class Endpoint {
   public:
    static void init();
    static uint64_t duplicateHandle(Task* source, Task* destination,
                                    uint64_t handle,
                                    uint64_t destinationHandle = 0);
    static Endpoint* create(EndpointService* service = nullptr);
    static void expireWaits(uint64_t now);
    static void cancelThread(Thread* thread);
    static void interruptThread(Thread* thread);
    static void closeOwner(Task* owner);
    void retain();
    void release();
    bool isClosed();
    void receiverClosed(Task* owner);
    bool notifyFromInterrupt(uint64_t type, uint64_t value);
    bool handlesContext(uint64_t operation) const;
    int callImmediate(IpcMessage& request, IpcMessage& reply, uint64_t deadline,
                      bool kernelOrigin = false);
    int call(IpcMessage* request, IpcMessage* reply,
             uint64_t deadline = UINT64_MAX);
    int callFromKernel(IpcMessage* request, IpcMessage* reply,
                       uint64_t deadline);
    int reply(IpcMessage* message);
    int receive(IpcMessage* message,
                uint64_t deadline = UINT64_MAX);
    int send(IpcMessage* message, uint64_t deadline = UINT64_MAX);
    int cancel(uint64_t threadId);
    bool requestActive(uint64_t token);
    bool grantAccess(Task* task, uint32_t rights);
    bool protectRecovery(Task& owner);
    bool adoptRecovery(Task& owner);
    void releaseRecovery(Task& owner);
    void destroy();

    uint64_t getId() const { return id; }
    uint64_t setId(uint64_t value) {
        id = value;
        return id;
    }
    Task* getOwner() const { return owner; }
    Task* setOwner(Task* value) {
        owner = value;
        return owner;
    }
    EndpointImplementation* getImplementation() const { return implementation; }
    EndpointImplementation* setImplementation(EndpointImplementation* value) {
        implementation = value;
        return implementation;
    }

   private:
    int callImpl(IpcMessage* request, IpcMessage* reply, uint64_t deadline,
                 bool kernelOrigin);
    uint64_t id;
    Task* owner;
    EndpointImplementation* implementation;
    uint32_t references   = 1;
    uint32_t recoveryRole = 0;
};

}  // namespace kernel

#include "service/common/kernel-requests.h"

namespace kernel {
class EndpointService {
   public:
    virtual int call(Task& caller, const IpcMessage& request,
                     IpcMessage& reply) = 0;
};
EndpointService& ipcKernelService();
EndpointService& memoryKernelService();
EndpointService& deviceKernelService();
EndpointService& systemKernelService();
EndpointService& taskKernelService();
EndpointService& pciKernelService();
class CoreServices {
   public:
    static bool init();
    static uint64_t getHandle(Task& caller, os::CoreServiceKind kind);
    static uint64_t bootstrap(Task& caller);
};
}  // namespace kernel

namespace kernel {
class MessageData {
   public:
    static bool name(const IpcMessage& message, uint32_t word,
                     const char*& value, size_t limit = 64) {
        if (message.length <= word) return false;
        value              = reinterpret_cast<const char*>(&message.data[word]);
        const size_t bytes = (message.length - word) * sizeof(uint64_t);
        for (size_t i = 0; i < bytes && i < limit; ++i)
            if (!value[i]) return i != 0;
        return false;
    }
    static void copy(void* destination, const void* source, size_t bytes) {
        auto* to   = static_cast<uint8_t*>(destination);
        auto* from = static_cast<const uint8_t*>(source);
        for (size_t i = 0; i < bytes; ++i) to[i] = from[i];
    }
    static void created(IpcMessage& reply, uint64_t handle) {
        if (handle) reply.handles[reply.handleCount++] = handle;
    }
};

}  // namespace kernel
