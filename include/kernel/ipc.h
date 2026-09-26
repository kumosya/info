#pragma once

#include <stdint.h>

#include "os/abi.h"

namespace kernel {

struct Process;
struct EndpointImplementation;

constexpr uint32_t endpointRightSend    = 1U << 0;
constexpr uint32_t endpointRightReceive = 1U << 1;
constexpr uint32_t endpointRightManage  = 1U << 2;

using IpcMessage = os::IpcMessage;

struct Endpoint {
    uint64_t id;
    Process* owner;
    EndpointImplementation* implementation;
};

void initIpc();
Endpoint* createEndpoint();
void destroyEndpoint(Endpoint* endpoint);
bool grantEndpointAccess(Endpoint* endpoint, Process* process, uint32_t rights);
uint64_t duplicateEndpointHandle(Process* source, Process* destination,
                                 uint64_t handle,
                                 uint64_t destinationHandle = 0);
int sendMessage(Endpoint* endpoint, IpcMessage* message);
int receiveMessage(Endpoint* endpoint, IpcMessage* message);
int replyMessage(Endpoint* endpoint, IpcMessage* message);
int callService(Endpoint* endpoint, IpcMessage* request, IpcMessage* reply);
bool notifyEndpointFromInterrupt(Endpoint* endpoint, uint64_t type,
                                 uint64_t value);

}  // namespace kernel
