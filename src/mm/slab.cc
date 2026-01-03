#include <cstdint>
#include <cstring>

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/tty.h"

namespace mm::slab {
void Init() {
    /* TODO */

    tty::printf("Slab initialized.\n");

    int *p = new int;
    *p     = 0x3456789a;
    tty::printf("p = 0x%lx, *p = 0x%x\n", (std::uint64_t)p, *p);
    delete p;
}
}  // namespace mm::slab