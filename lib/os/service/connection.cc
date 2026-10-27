#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include <os/mm.h>
#include <os/service.h>

namespace os {
bool Sys::Connection::connect(const char* name, uint64_t registry) {
    if (endpoint_.getValid() || !name ||
        Runtime::stringLength(name) >= sizeof(name_))
        return false;
    Runtime::copyString(name_, name, sizeof(name_));
    registry_ = registry ? registry : os::detail::RegistryClient().endpoint();
    endpoint_.reset(
        os::detail::RegistryClient(registry_).lookup(name_, 1, &generation_));
    return endpoint_.getValue() != 0;
}

int Sys::Connection::call(IpcMessage& request, IpcMessage& reply,
                          uint64_t deadline) {
    if (!endpoint_.getValue() && !connect(name_, registry_))
        return static_cast<int>(ErrorCode::NotFound);
    const int status =
        callService(endpoint_.getValue(), request, reply, deadline);
    if (status == static_cast<int>(ErrorCode::EndpointClosed) ||
        status == static_cast<int>(ErrorCode::PeerDied) ||
        status == static_cast<int>(ErrorCode::InvalidHandle))
        endpoint_.reset();
    // A failed request is never automatically replayed: its service-side
    // effects may already have committed before the transport failed.
    return status;
}

void Sys::Connection::disconnect() { endpoint_.reset(); }

bool Sys::Connection::getConnected() const { return endpoint_.getValid(); }
const char* Sys::Connection::getName() const { return name_; }
uint64_t Sys::Connection::getGeneration() const { return generation_; }

}  // namespace os
