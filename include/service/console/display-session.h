/*
 * 图形显示会话切换。
 */

#pragma once
#include <cstdint>
namespace os {
    enum class GraphicalPhase : uint32_t {
        Starting,
        Greeter,
        Authenticating,
        Desktop,
        Suspended
    };
    enum class GraphicalRequest : uint64_t {
        Bind = 0x47530300,
        State,
        GreeterReady,
        Authenticated,
        DesktopReady,
        Reset,
        Suspend,
        Resume
    };
    /* 协调图形会话建立、显示资源交接与退出恢复。 */
    class GraphicalSession {
        public:
            static bool bind(uint64_t endpoint, uint64_t task,
                             uint64_t started);
            static bool state(uint64_t endpoint, GraphicalPhase& phase,
                              uint64_t& controller);
            static bool command(uint64_t endpoint, GraphicalRequest operation,
                                uint64_t window = 0);
    };
}  // namespace os
