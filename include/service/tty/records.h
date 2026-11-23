/*
 * 服务通信数据布局。
 */

#pragma once
#include <cstdint>

namespace services::wire {

    /* 保存终端输入、输出和本地处理标志，以及控制字符配置。 */
    struct TerminalAttributes {
            bool canonicalMode, echo, signalMode;
            uint8_t eraseCharacter, interruptCharacter, eofCharacter,
                suspendCharacter, quitCharacter;
    };

    /* 保存终端字符行列数及可选像素尺寸。 */
    struct TerminalWindowSize {
            uint32_t rows, columns, pixelWidth, pixelHeight;
    };

    static_assert(sizeof(TerminalAttributes) == 8);
    static_assert(sizeof(TerminalWindowSize) == 16);
}  // namespace services::wire
