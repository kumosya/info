/*
 * 控制台客户端与启动显示交接。
 */

#pragma once
#include <cstdint>
namespace os {
    constexpr uint32_t textTerminalCount   = 6;
    constexpr uint32_t graphicalTerminalId = 6;

    // OS display handoff protocol; independent of GUI widgets and
    // authentication.
    enum class GraphicalDisplayRequest : uint64_t {
        SetActive = 0x30d,
        GetActive = 0x47530400
    };

    // Boot display belongs to console; only init may report or finish startup.
    /* 向启动控制台报告文字和启动进度，并处理显示服务接管。 */
    class BootConsole {
        public:
            static bool report(uint64_t endpoint, const char* name,
                               bool started, int32_t error = 0);
            static bool finish(uint64_t endpoint, uint32_t terminal);
            static bool state(uint64_t endpoint, bool& booting,
                              uint32_t& preferredTerminal);
    };

    /* 保存显示缓冲区从启动控制台交给正式显示服务所需的信息。 */
    class DisplayHandoff {
        public:
            bool setGraphicalActive(uint64_t windowService, bool active);
    };
}  // namespace os
