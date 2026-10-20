#ifndef _OS_PTY_H
#define _OS_PTY_H

#include <cstddef>
#include <cstdint>

namespace os {

struct PtyHandles {
    uint64_t masterHandle;
    uint64_t slaveHandle;
};

struct TerminalAttributes {
    bool canonicalMode;
    bool echo;
    bool signalMode;
    uint8_t eraseCharacter;
    uint8_t interruptCharacter;
    uint8_t eofCharacter;
    uint8_t suspendCharacter;
    uint8_t quitCharacter;
};

struct TerminalWindowSize {
    uint32_t rows;
    uint32_t columns;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
};

class Pty {
   public:
    static bool getPtyNumber(uint64_t handle, uint32_t& number);
    static bool setPtyNonblocking(uint64_t handle, bool enabled);
    static bool setPtyForegroundGroup(uint64_t handle, uint64_t taskGroupId);
    static bool setPtyWindowSize(uint64_t handle, const TerminalWindowSize& size);
    static bool getPtyWindowSize(uint64_t handle, TerminalWindowSize& size);
    static bool setPtyAttributes(uint64_t handle,
                      const TerminalAttributes& attributes);
    static bool getPtyAttributes(uint64_t handle, TerminalAttributes& attributes);
    static bool createPty(PtyHandles& handles);
};

}  // namespace os

#endif
