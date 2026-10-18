#ifndef _OS_TERMINAL_H
#define _OS_TERMINAL_H

#include <stddef.h>
#include <stdint.h>

namespace os {

constexpr uint32_t textTerminalCount = 6;
constexpr uint32_t graphicalTerminalId = 6;

class Terminal {
   public:
    static uint32_t getId();
    static void setId(uint32_t id);
    static int64_t read(void* buffer, size_t size);
    static int64_t write(const void* buffer, size_t size);
    static uint32_t getActive();
    static bool switchTo(uint32_t id);
    static bool setForegroundTask(uint64_t taskId);
    static void initialize();

   private:
    static uint64_t endpoint_;
    static uint32_t id_;
};

}  // namespace os

#endif
