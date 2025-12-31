#include "kassert.h"
#include "tty.h"

void kassert_fail(const char *expr, const char *file, int line,
                  const char *msg) {
    // Use printf to ensure this works early
    tty::printf("KERNEL ASSERT FAILED: %s at %s:%d\n", expr, file, line);
    if (msg) tty::printf("Message: %s\n", msg);
    // Halt
    while (true) asm __volatile__("hlt");
}
