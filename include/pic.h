#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* 8259 PIC ports */
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

namespace pic {
    void remap(int offset1, int offset2);
    void mask(uint8_t master_mask, uint8_t slave_mask);
    void init();
    void mask_irq(int irq);
    void unmask_irq(int irq);
}

/* ICW control words */
#define ICW1_INIT 0x11
#define ICW4_8086 0x01

#endif // PIC_H
