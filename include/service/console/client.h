#pragma once
#include <cstdint>
namespace os {
constexpr uint32_t textTerminalCount   = 6;
constexpr uint32_t graphicalTerminalId = 6;

// OS display handoff protocol; independent of GUI widgets and authentication.
enum class GraphicalDisplayRequest : uint64_t {
    SetActive = 0x30d,
    GetActive = 0x47530400
};

// Boot display belongs to console; only init may report or finish startup.
class BootConsole {
   public:
    static bool report(uint64_t endpoint, const char* name, bool started,
                       int32_t error = 0);
    static bool finish(uint64_t endpoint, uint32_t terminal);
    static bool state(uint64_t endpoint, bool& booting,
                      uint32_t& preferredTerminal);
};

class DisplayHandoff {
   public:
    bool setGraphicalActive(uint64_t windowService, bool active);
};
}  // namespace os
