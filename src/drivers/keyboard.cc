#include "cpu.h"
#include "io.h"
#include "tty.h"
#include "keyboard.h"

#include <cstdint>

using namespace std;

extern "C" void kbd_handler_c();

// Stub implemented in interrupt.S
extern "C" void kbd_stub(void);

extern "C" void kbd_handler_c() {
    // Read scancode from port 0x60
    uint8_t sc = inb(0x60);
    // Print scancode in hex (keep short)
    tty::printf("kbd:%02x\n", sc);
    // Send EOI to PIC
    outb(PIC1_CMD, 0x20);
}

namespace keyboard {
void init() {
    // Unmask IRQ1 (keyboard)
    pic::unmask_irq(1);
}
} // namespace keyboard
