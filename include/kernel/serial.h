#pragma once

#include <cstdint>

namespace kernel::serial {

void initSerial();
void serialWrite(char value);
void serialWrite(const char* value);
void serialWriteHex(uint64_t value);
void serialWriteDecimal(uint64_t value);
void emergencyWrite(const char* value);
void emergencyHex(uint64_t value);

}  // namespace kernel::serial
