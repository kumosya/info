#include "start.h"

#include <cstdint>

#include "cpu.h"
#include "multiboot2.h"
#include "tty.h"

static idt::Entry idt_ety[256];

void KernelMain(std::uint8_t *addr);
// Stubs implemented in interrupt.S
extern "C" void gp_fault_stub(void);
extern "C" void page_fault_stub(void);

static void SetIDTEntry(int vec, void *handler, std::uint16_t sel, std::uint8_t type_attr) {
    std::uint64_t addr       = reinterpret_cast<std::uint64_t>(handler);
    idt_ety[vec].offset_low  = addr & 0xFFFF;
    idt_ety[vec].selector    = sel;
    idt_ety[vec].ist         = 0;
    idt_ety[vec].type_attr   = type_attr;
    idt_ety[vec].offset_mid  = (addr >> 16) & 0xFFFF;
    idt_ety[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt_ety[vec].zero        = 0;
}

extern "C" void boot_gp_fault_handler() {
    boot::printf("#GP Fault!\n");
    // Halt so user can inspect
    while (true) {
        asm volatile("hlt");
    }
}
// C handler called from the assembly stub. Prints CR2 and halts.
extern "C" void boot_page_fault_handler(std::uint64_t fault_addr) {
    boot::printf("Page Fault! CR2=0x%lx\n", fault_addr);
    // Halt so user can inspect
    while (true) {
        asm volatile("hlt");
    }
}

/*  Check if MAGIC is valid and print the Multiboot information structure
   pointed by ADDR. */
extern "C" void CppStart(std::uint32_t magic, std::uint8_t *addr) {
#if ENABLE_TEXT_OUTPUT == true
    boot::videoInit();
#endif

    //  Am I booted by a Multiboot-compliant boot loader?
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        boot::printf("Error: Invalid magic number: 0x%x.\n", (unsigned)magic);
        while (true);
    }
    if (addr[0] & 7) {
        boot::printf("Error: Unaligned mbi: 0x%x.\n", addr);
        while (true);
    }
    // Install minimal IDT and page-fault handler before enabling mappings
    // zero out idt
    boot::mm::memset(&idt_ety, 0, sizeof(idt_ety));
    // kernel code selector is 0x08 (see gdt setup in boot.S)
    //		boot::printf("handler set for page fault at 0x%lx\n", (uint64_t)page_fault_stub);
    SetIDTEntry(13, (void *)gp_fault_stub, 0x08, 0x8E);
    SetIDTEntry(14, (void *)page_fault_stub, 0x08, 0x8E);
    idt::Ptr idtp;
    idtp.limit = sizeof(idt_ety) - 1;
    idtp.base  = reinterpret_cast<std::uint64_t>(&idt_ety);
    asm __volatile__("lidt %0" : : "m"(idtp));

    boot::mm::Init(addr);
    KernelMain(addr);
}
