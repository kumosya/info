#pragma once

#include <stddef.h>
#include <stdint.h>

#include "os/abi.h"
#include "os/pty.h"

namespace kernel {

class Task;
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
    static Endpoint* create();
    bool notifyFromInterrupt(uint64_t type, uint64_t value);
    int call(IpcMessage* request, IpcMessage* reply);
    int reply(IpcMessage* message);
    int receive(IpcMessage* message);
    int send(IpcMessage* message);
    bool grantAccess(Task* task, uint32_t rights);
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
    uint64_t id;
    Task* owner;
    EndpointImplementation* implementation;
};

class MessageQueueAttributes {
   public:
    MessageQueueAttributes() = default;
    MessageQueueAttributes(uint64_t flagsValue, uint64_t maximumMessagesValue,
                           uint64_t messageSizeValue,
                           uint64_t currentMessagesValue)
        : flags(flagsValue),
          maximumMessages(maximumMessagesValue),
          messageSize(messageSizeValue),
          currentMessages(currentMessagesValue) {}
    uint64_t getFlags() const { return flags; }
    uint64_t setFlags(uint64_t value) {
        flags = value;
        return flags;
    }

   private:
    uint64_t flags;
    uint64_t maximumMessages;
    uint64_t messageSize;
    uint64_t currentMessages;
};

class Pipe {
   public:
    static bool createHandles(Task& task, uint64_t& readHandle,
                              uint64_t& writeHandle);
    static bool createChannelPair(Task& task, uint64_t& firstHandle,
                                  uint64_t& secondHandle);
    static uint64_t channelIdentity(Task& task, uint64_t handle);
    static bool setChannelNonblocking(Task& task, uint64_t handle,
                                      bool enabled);
    static bool signalChannelEof(Task& task, uint64_t handle);
    static uint64_t duplicateHandle(Task& source, Task& destination,
                                    uint64_t handle,
                                    uint64_t destinationHandle = 0);
    static int64_t read(Task& task, uint64_t handle, void* buffer, size_t size);
    static int64_t write(Task& task, uint64_t handle, const void* buffer,
                         size_t size);
    static void closeHandleObject(void* object);
    static void closeChannelHandleObject(void* object);
};

class MessageQueue {
   public:
    static uint64_t open(Task& task, const char* name, int flags,
                         uint32_t maximumMessages, uint32_t messageSize);
    static uint64_t duplicateHandle(Task& source, Task& destination,
                                    uint64_t handle,
                                    uint64_t destinationHandle = 0);
    static bool unlink(const char* name);
    static int send(Task& task, uint64_t handle, const void* data, size_t size,
                    uint32_t priority);
    static int64_t receive(Task& task, uint64_t handle, void* data,
                           size_t capacity, uint32_t& priority);
    static bool getAttributes(Task& task, uint64_t handle,
                              MessageQueueAttributes& attributes);
    static bool setFlags(Task& task, uint64_t handle, uint64_t flags,
                         uint64_t& oldFlags);
    static int registerNotification(Task& task, uint64_t handle, int type,
                                    uint32_t signal);
    static void closeHandleObject(void* object);
};
}  // namespace kernel
