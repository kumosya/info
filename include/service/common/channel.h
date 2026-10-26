#pragma once
#include <os/service.h>
namespace services {
class RequestChannel : public os::Sys::Transport {
   public:
    explicit RequestChannel(os::CoreServiceKind kind)
        : os::Sys::Transport(kind) {}
    using os::Sys::Transport::call;
    using os::Sys::Transport::openHandle;
    using os::Sys::Transport::result;
};
}  // namespace services
