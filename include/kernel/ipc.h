#pragma once

#include <cstddef>
#include <cstdint>

#include "os/abi.h"
#include "os/pty.h"

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
    int call(IpcMessage* request, IpcMessage* reply,
             uint64_t deadline = os::ipcInfiniteDeadline);
    int callFromKernel(IpcMessage* request, IpcMessage* reply, uint64_t deadline);
    int reply(IpcMessage* message);
    int receive(IpcMessage* message, uint64_t deadline = os::ipcInfiniteDeadline);
    int send(IpcMessage* message, uint64_t deadline = os::ipcInfiniteDeadline);
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
    int callImpl(IpcMessage* request, IpcMessage* reply, uint64_t deadline, bool kernelOrigin);
    uint64_t id;
    Task* owner;
    EndpointImplementation* implementation;
    uint32_t references = 1;
    uint32_t recoveryRole = 0;
};

}  // namespace kernel
