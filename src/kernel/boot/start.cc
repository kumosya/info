#include "start.h"
#include "multiboot2.h"
#include "tty.h"

#include <cstdint>

void kernel_main(uint8_t *addr);

/*  Check if MAGIC is valid and print the Multiboot information structure
   pointed by ADDR. */
extern "C" void cppstart(uint32_t magic, uint8_t *addr) {
#if ENABLE_TEXT_OUTPUT == true
    boot::video_init();
#endif

    //  Am I booted by a Multiboot-compliant boot loader?
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        boot::printf("Error: Invalid magic number: 0x%x.\n", (unsigned)magic);
        while (true)
            ;
    }
    if (addr[0] & 7) {
        boot::printf("Error: Unaligned mbi: 0x%x.\n", addr);
        while (true)
            ;
    }

    boot::mm::init(addr);
    kernel_main(addr);
}
