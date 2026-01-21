#include "kernel/kassert.h"
#include "kernel/tty.h"

void kassert_fail(const char *expr, const char *file, int line,
                  const char *msg) {
    // Use printk to ensure this works early
    tty::printk("KERNEL ASSERT FAILED: %s at %s:%d\n", expr, file, line);
    if (msg) tty::printk("Message: %s\n", msg);
    // Halt
    while (true) asm __volatile__("hlt");
}
