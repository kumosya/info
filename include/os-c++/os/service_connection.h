#pragma once

#include "os/abi.h"
#include "os/protocol.h"
#include "os/service.h"

namespace os {
class ServiceConnection {
   public:
    bool connect(const char* name, uint64_t registry = 0) {
        if (!name || Runtime::stringLength(name) >= sizeof(name_)) return false;
        Runtime::copyString(name_, name, sizeof(name_));
        registry_ = registry ? registry : ServiceRegistry::serviceRegistry();
        endpoint_.reset(ServiceRegistry::lookupService(registry_, name_, 1, &generation_));
        return endpoint_.get() != 0;
    }
    int call(IpcMessage& request, IpcMessage& reply, uint64_t deadline = ipcInfiniteDeadline) {
        if (!endpoint_.get() && !connect(name_, registry_)) return static_cast<int>(ErrorCode::NotFound);
        const int status = callService(endpoint_.get(), request, reply, deadline);
        if (status == static_cast<int>(ErrorCode::EndpointClosed) ||
            status == static_cast<int>(ErrorCode::PeerDied) ||
            status == static_cast<int>(ErrorCode::InvalidHandle)) endpoint_.reset();
        // A failed request is never automatically replayed: its service-side
        // effects may already have committed before the transport failed.
        return status;
    }
    void disconnect() { endpoint_.reset(); }
    uint64_t generation() const { return generation_; }

   private:
    Handle endpoint_;
    char name_[serviceNameCapacity]{};
    uint64_t registry_ = 0;
    uint64_t generation_ = 0;
};
}  // namespace os
