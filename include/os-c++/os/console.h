/*
 * 终端输入输出。
 */

#ifndef _OS_CONSOLE_H
#define _OS_CONSOLE_H
#include <os/ipc.h>

#include <cstddef>
#include <cstdint>

namespace os {

    /* 封装终端输出、输入和显示控制请求。 */
    class Console {
        public:
            /* 逐字符解析终端转义序列，输出已识别的控制动作。 */
            class EscapeParser {
                public:
                    enum class Result : int {
                        Text = 0,
                        Pending = 1,
                        Command = 2
                    };
                    Result feed(uint8_t value);
                    void reset();
                    char getCommand() const;
                    uint32_t getParameterCount() const;
                    uint16_t getParameter(uint32_t index) const;

                private:
                    uint16_t parameters_[8]{};
                    uint8_t count_{};
                    uint8_t state_{};
                    char command_{};
            };

            /* 保存终端文字的颜色和显示属性。 */
            class Style {
                public:
                    void apply(uint16_t value);
                    void reset();
                    uint8_t getForeground() const;
                    uint8_t getBackground() const;
                    uint8_t getAttributes() const;
                    void setForeground(uint8_t value);
                    void setBackground(uint8_t value);
                    void setAttributes(uint8_t value);

                private:
                    uint8_t foreground_{7};
                    uint8_t background_{};
                    uint8_t attributes_{};
            };

            Console();
            ~Console();
            Console(const Console&) = delete;
            Console& operator=(const Console&) = delete;
            Console& operator=(Console&& other) noexcept;
            int open(uint32_t id);
            int openCurrent();
            int close();
            int64_t read(void* buffer, size_t size);
            int64_t write(const void* buffer, size_t size);
            int activate();
            int refresh();
            static uint32_t terminalColor(uint8_t index, bool bright = false);

            uint32_t getId() const;
            bool getOpened() const;
            bool getActive() const;
            bool getEcho() const;
            uint64_t getForegroundGroup() const;
            bool getValid() const;
            int getError() const;

            int setEcho(bool enabled);
            int setForegroundGroup(uint64_t group);

        private:
            uint64_t endpoint_{};
            uint32_t id_{};
            bool valid_{};
            bool active_{};
            bool echo_{};
            uint64_t foregroundGroup_{};
            int error_{};
    };

    constexpr uint32_t textTerminalCount = 6;
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
            explicit BootConsole(uint64_t endpoint, bool owned = false);
            ~BootConsole() = default;
            bool report(const char* name, bool started, int32_t error = 0);
            bool finish(uint32_t terminal);
            bool state(bool& booting, uint32_t& preferredTerminal);

        private:
            Ipc::Handle endpoint_;
    };

    /* 保存显示缓冲区从启动控制台交给正式显示服务所需的信息。 */
    class DisplayHandoff {
        public:
            explicit DisplayHandoff(uint64_t windowService, bool owned = false);
            ~DisplayHandoff() = default;
            bool setGraphicalActive(bool active);

        private:
            Ipc::Handle endpoint_;
    };

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
            explicit GraphicalSession(uint64_t endpoint, bool owned = false);
            ~GraphicalSession() = default;
            bool bind(uint64_t task, uint64_t started);
            bool state(GraphicalPhase& phase, uint64_t& controller);
            bool command(GraphicalRequest operation, uint64_t window = 0);

        private:
            Ipc::Handle endpoint_;
    };

}  // namespace os
#endif
