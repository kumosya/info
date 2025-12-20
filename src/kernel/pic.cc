#include "io.h"

#include <cstdint>

using namespace std;

namespace pic {
void remap(int offset1, int offset2) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    // starts the initialization sequence (in cascade mode)
    outb(PIC1_CMD, ICW1_INIT);
    outb(PIC2_CMD, ICW1_INIT);

    // set vector offsets
    outb(PIC1_DATA, offset1);
    outb(PIC2_DATA, offset2);

    // tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    outb(PIC1_DATA, 0x04);
    // tell Slave PIC its cascade identity (0000 0010)
    outb(PIC2_DATA, 0x02);

    // environment info
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // restore saved masks
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

void mask(uint8_t master_mask, uint8_t slave_mask) {
    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

void init() {
    // Remap PIC to avoid conflicts with CPU exceptions (0x20..)
    remap(0x20, 0x28);
    // Mask all IRQs by default
    mask(0xFF, 0xFF);

    /* Enable hardware interrupts now that PIC and IDT are set up */
    asm volatile("sti");
}

void mask_irq(int irq) {
    if (irq < 8) {
        uint8_t mask = inb(PIC1_DATA);
        mask |= (1 << irq);
        outb(PIC1_DATA, mask);
    } else {
        uint8_t mask = inb(PIC2_DATA);
        mask |= (1 << (irq - 8));
        outb(PIC2_DATA, mask);
    }
}

void unmask_irq(int irq) {
    if (irq < 8) {
        uint8_t mask = inb(PIC1_DATA);
        mask &= ~(1 << irq);
        outb(PIC1_DATA, mask);
    } else {
        uint8_t mask = inb(PIC2_DATA);
        mask &= ~(1 << (irq - 8));
        outb(PIC2_DATA, mask);
    }
}
} // namespace pic
