#include <cstdint>

#include "kernel/io.h"

namespace pic {
void Remap(int offset1, int offset2) {
    std::uint8_t a1 = inb(PIC1_DATA);
    std::uint8_t a2 = inb(PIC2_DATA);

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

void Mask(std::uint8_t master_mask, std::uint8_t slave_mask) {
    outb(PIC1_DATA, master_mask);
    outb(PIC2_DATA, slave_mask);
}

void Init() {
    // Remap PIC to avoid conflicts with CPU exceptions (0x20..)
    Remap(0x20, 0x28);
    // Mask IRQs
    Mask(0xfd, 0xff);

    /* Enable hardware interrupts now that PIC and IDT are set up */
    asm volatile("sti");
}

void MaskIrq(int irq) {
    if (irq < 8) {
        std::uint8_t mask = inb(PIC1_DATA);
        mask |= (1 << irq);
        outb(PIC1_DATA, mask);
    } else {
        std::uint8_t mask = inb(PIC2_DATA);
        mask |= (1 << (irq - 8));
        outb(PIC2_DATA, mask);
    }
}

void UnmaskIrq(int irq) {
    if (irq < 8) {
        std::uint8_t mask = inb(PIC1_DATA);
        mask &= ~(1 << irq);
        outb(PIC1_DATA, mask);
    } else {
        std::uint8_t mask = inb(PIC2_DATA);
        mask &= ~(1 << (irq - 8));
        outb(PIC2_DATA, mask);
    }
}
}  // namespace pic
