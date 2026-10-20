#pragma once

#include "kernel/ipc.h"
#include "os/core_service.h"

namespace kernel {
class EndpointService {
   public:
    virtual int call(Task& caller, const IpcMessage& request, IpcMessage& reply) = 0;
};
EndpointService& memoryKernelService();
EndpointService& deviceKernelService();
EndpointService& systemKernelService();
EndpointService& taskKernelService();
EndpointService& pciKernelService();
class CoreServices {
   public:
    static bool initialize();
    static uint64_t getHandle(Task& caller, os::CoreServiceKind kind);
};
}  // namespace kernel
