#include "service/common/records.h"
#pragma once
#include "service/console/client.h"
#include <os/ipc.h>
#include <os/task.h>
#include <os/console.h>

namespace console {
// Validate the complete wire payload before allowing any framebuffer or TTY change.
class BootState {
    bool active_ = true;
 public:
    bool active() const { return active_; }
    static bool authorized(const os::wire::TaskInfo& caller) {
        return caller.managedService && caller.serviceRole == static_cast<uint32_t>(os::ServiceRole::Init);
    }
    static bool validStatus(const os::IpcMessage& request) {
        if (request.length != 10 || request.handleCount || request.data[0] > 1) return false;
        const auto* name = reinterpret_cast<const unsigned char*>(&request.data[2]);
        if (!name[0]) return false;
        for (unsigned i = 0; i < 64; ++i) {
            if (!name[i]) return true;
            if (name[i] < 32 || name[i] > 126) return false;
        }
        return false;
    }
    static bool validComplete(const os::IpcMessage& request) {
        return request.length == 1 && !request.handleCount && request.data[0] <= os::graphicalTerminalId;
    }
    // The caller commits this only after display handoff has succeeded.
    void complete() { active_ = false; }
};
}
