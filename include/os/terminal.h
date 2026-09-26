#pragma once

#include <stddef.h>
#include <stdint.h>

namespace os {

constexpr uint32_t textTerminalCount = 6;
constexpr uint32_t graphicalTerminalId = 6;

uint32_t getTerminalId();
void setTerminalId(uint32_t terminalId);
int64_t readTerminal(void* buffer, size_t size);
int64_t writeTerminal(const void* buffer, size_t size);
uint32_t getActiveTerminal();
bool switchTerminal(uint32_t terminalId);
bool setTerminalForegroundProcess(uint64_t processId);

}  // namespace os
