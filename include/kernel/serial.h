#pragma once

#include <stdint.h>

namespace kernel::serial {

void initSerial();
void serialWrite(char value);
void serialWrite(const char* value);
void serialWriteHex(uint64_t value);
void serialWriteDecimal(uint64_t value);

}  // namespace kernel::serial
