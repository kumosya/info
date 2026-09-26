#pragma once

#include <stdint.h>

namespace kernel {

int waitOnUserAddress(volatile uint32_t* address, uint32_t expected);
uint32_t wakeUserAddress(volatile uint32_t* address, uint32_t count);

}  // namespace kernel
