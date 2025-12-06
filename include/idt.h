#ifndef _IDT_H_
#define _IDT_H_

#include <stdint.h>

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

namespace idt {
    void ge_fault_handler();
    void page_fault_handler(uint64_t fault_addr);

    void init();
    static void set_entry(int vec, void* handler, uint16_t sel, uint8_t type_attr);
}

#endif /* _IDT_H_ */