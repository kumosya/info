#pragma once

#include <stdint.h>

namespace kernel {

void initSerial();
void serialWrite(char value);
void serialWrite(const char* value);
void serialWriteHex(uint64_t value);
void serialWriteDecimal(uint64_t value);

}  // namespace kernel
