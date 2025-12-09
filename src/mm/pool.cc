#include "mm.h"
#include "page.h"
#include "tty.h"

#include <cstdint>
#include <cstring>

namespace mm::pool {
void init() { 
    tty::printf("Memory pool initialized.\n"); }
} // namespace mm::pool