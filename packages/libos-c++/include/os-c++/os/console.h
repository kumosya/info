#ifndef _OS_CONSOLE_H
#define _OS_CONSOLE_H
#include <cstddef>
#include <cstdint>

namespace os {
class Console {
   public:
    class EscapeParser {
       public:
        enum class Result : int { Text = 0, Pending = 1, Command = 2 };
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
    Console(const Console&)            = delete;
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
}  // namespace os
#endif
