#pragma once

#include <stddef.h>
#include <stdint.h>

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

bool createPty(PtyHandles& handles);
bool getPtyAttributes(uint64_t handle, TerminalAttributes& attributes);
bool setPtyAttributes(uint64_t handle,
                      const TerminalAttributes& attributes);
bool getPtyWindowSize(uint64_t handle, TerminalWindowSize& size);
bool setPtyWindowSize(uint64_t handle, const TerminalWindowSize& size);
bool setPtyForegroundGroup(uint64_t handle, uint64_t processGroupId);
bool setPtyNonblocking(uint64_t handle, bool enabled);

}  // namespace os
