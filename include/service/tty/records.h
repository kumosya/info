#pragma once
#include <cstdint>
namespace os::wire {
struct TerminalAttributes {
    bool canonicalMode, echo, signalMode;
    uint8_t eraseCharacter, interruptCharacter, eofCharacter, suspendCharacter,
        quitCharacter;
};
struct TerminalWindowSize {
    uint32_t rows, columns, pixelWidth, pixelHeight;
};
static_assert(sizeof(TerminalAttributes) == 8);
static_assert(sizeof(TerminalWindowSize) == 16);
}  // namespace os::wire
