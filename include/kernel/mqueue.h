#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kernel {

class Process;

struct MessageQueueAttributes {
    uint64_t flags;
    uint64_t maximumMessages;
    uint64_t messageSize;
    uint64_t currentMessages;
};

uint64_t openMessageQueue(Process& process, const char* name, int flags,
                          uint32_t maximumMessages, uint32_t messageSize);
uint64_t duplicateMessageQueueHandle(Process& source, Process& destination,
                                     uint64_t handle,
                                     uint64_t destinationHandle = 0);
bool unlinkMessageQueue(const char* name);
int sendMessageQueue(Process& process, uint64_t handle, const void* data,
                     size_t size, uint32_t priority);
int64_t receiveMessageQueue(Process& process, uint64_t handle, void* data,
                            size_t capacity, uint32_t& priority);
bool getMessageQueueAttributes(Process& process, uint64_t handle,
                               MessageQueueAttributes& attributes);
bool setMessageQueueFlags(Process& process, uint64_t handle, uint64_t flags,
                          uint64_t& oldFlags);
int registerMessageQueueNotification(Process& process, uint64_t handle,
                                     int type, uint32_t signal);
void closeMessageQueueHandleObject(void* object);

}  // namespace kernel
