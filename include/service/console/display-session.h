#pragma once
#include <cstdint>
namespace os {
enum class GraphicalPhase : uint32_t { Starting, Greeter, Authenticating, Desktop };
enum class GraphicalRequest : uint64_t { Bind = 0x47530300, State, GreeterReady, Authenticated, DesktopReady, Reset };
class GraphicalSession {
 public:
    static bool bind(uint64_t endpoint, uint64_t task, uint64_t started);
    static bool state(uint64_t endpoint, GraphicalPhase& phase, uint64_t& controller);
    static bool command(uint64_t endpoint, GraphicalRequest operation, uint64_t window = 0);
};
}
