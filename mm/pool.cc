#include <mm.h>
#include <page.h>
#include <tty.h>

#include <stdint.h>
#include <string.h>

namespace mm::pool {
    void init() {
        tty::printf("Memory pool initialized.\n");
    }
}