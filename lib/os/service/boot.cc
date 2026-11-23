#include <os/console.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>

#include "service/common/protocol.h"

namespace os {

    /* 连接对象保存启动终端端点，并按 owned 决定是否负责关闭。 */
    BootConsole::BootConsole(uint64_t endpoint, bool owned)
        : endpoint_(endpoint, owned) {
    }

    bool BootConsole::report(const char* name, bool started, int32_t error) {
        const uint64_t endpoint = endpoint_.getValue();
        if (!endpoint || !name || !*name || Runtime::stringLength(name) >= 64) {
            return false;
        }

        IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(services::TerminalRequest::BootStatus);
        request.length = 10;
        request.data[0] = started ? 1 : 0;
        request.data[1] = static_cast<int64_t>(error);
        Runtime::copyString(reinterpret_cast<char*>(&request.data[2]), name,
                            64);
        return !callService(endpoint, request, reply,
                            Sys().readMonotonicTime() + 250000000ULL) &&
               reply.length >= 1 && !reply.data[0];
    }

    bool BootConsole::finish(uint32_t terminal) {
        const uint64_t endpoint = endpoint_.getValue();
        if (!endpoint || terminal > graphicalTerminalId) {
            return false;
        }

        IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(services::TerminalRequest::BootComplete);
        request.length = 1;
        request.data[0] = terminal;
        return !callService(endpoint, request, reply,
                            Sys().readMonotonicTime() + 3000000000ULL) &&
               reply.length >= 1 && !reply.data[0];
    }

    bool BootConsole::state(bool& booting, uint32_t& preferredTerminal) {
        const uint64_t endpoint = endpoint_.getValue();
        IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(services::TerminalRequest::GetBootState);

        if (!endpoint ||
            callService(endpoint, request, reply,
                        Sys().readMonotonicTime() + 250000000ULL) ||
            reply.length != 3 || reply.data[0] ||
            reply.data[2] > graphicalTerminalId) {
            return false;
        }

        booting = reply.data[1] != 0;
        preferredTerminal = static_cast<uint32_t>(reply.data[2]);
        return true;
    }

    /* 一个交接对象对应一个窗口服务连接。 */
    DisplayHandoff::DisplayHandoff(uint64_t windowService, bool owned)
        : endpoint_(windowService, owned) {
    }

    bool DisplayHandoff::setGraphicalActive(bool active) {
        const uint64_t windowService = endpoint_.getValue();
        if (!windowService) {
            return false;
        }

        IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(GraphicalDisplayRequest::SetActive);
        request.data[0] = active ? 1 : 0;
        request.length = 1;

        if (callService(windowService, request, reply,
                        Sys().readMonotonicTime() + 1000000000ULL) == 0 &&
            reply.length >= 1 && reply.data[0] == 0) {
            return true;
        }

        // Reconcile a lost acknowledgement before console commits its own
        // active TTY.
        request = {};
        reply = {};
        request.type =
            static_cast<uint64_t>(GraphicalDisplayRequest::GetActive);
        return !callService(windowService, request, reply,
                            Sys().readMonotonicTime() + 1000000000ULL) &&
               reply.length == 2 && !reply.data[0] &&
               reply.data[1] == uint64_t(active);
    }

}  // namespace os
