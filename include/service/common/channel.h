/*
 * 服务请求构造与调用。
 */

#pragma once
#include <os/service.h>
namespace services {
    /* 构造到核心服务的请求，统一处理端点调用、返回码和回复。 */
    class RequestChannel : public os::Sys::Transport {
        public:
            explicit RequestChannel(os::CoreServiceKind kind)
                : os::Sys::Transport(kind) {}
            using os::Sys::Transport::call;
            using os::Sys::Transport::openHandle;
            using os::Sys::Transport::result;
    };
}  // namespace services
