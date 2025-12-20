#include "mm.h"
#include "page.h"
#include "tty.h"

#include <cstdint>
#include <cstring>


namespace mm::slab {
void init() {

    tty::printf("Slab initialized.\n"); }
} // namespace mm::slab