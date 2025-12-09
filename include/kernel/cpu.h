#ifndef CPU_H
#define CPU_H

/* CR0 */
#define CR0_PG (1 << 31)

/* CR4 */
#define CR4_PSE (1 << 4)
#define CR4_PAE (1 << 5)
#define CR4_PGE (1 << 7)

/* Segment selector */
#define SELECTOR_RPL (0)
#define SELECTOR_TI (2)
#define SELECTOR_INDEX (3)

#ifndef ASM_FILE

#include <cstdint>

using namespace std;

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr_t {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct gdt_ptr_t {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

namespace idt {
void init();
static void set_entry(int vec, void *handler, uint16_t sel, uint8_t type_attr);
} // namespace idt

namespace gdt {
void set_entry(int index, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran);
void init();
} // namespace gdt

#endif /* ASM_FILE */

#endif /* CPU_H */