#include <multiboot2.h>
#include <entry.h>

#include <cstdint>

void cppstart (uint32_t magic, uint8_t *addr);
void cppinit(void);

// 外部符号声明
extern char __kernel_start;

/*  Check if MAGIC is valid and print the Multiboot information structure
   pointed by ADDR. */
void
cppstart (uint32_t magic, uint8_t *addr)
{
	boot::video_init();
	
	//  Am I booted by a Multiboot-compliant boot loader? 
	if (magic != MULTIBOOT2_BOOTLOADER_MAGIC)
	{
		boot::printf ("Error: Invalid magic number: 0x%x.\n", (unsigned) magic);
		while (true);
	}
	if (addr[0] & 7)
	{
		boot::printf ("Error: Unaligned mbi: 0x%x.\n", addr);
		while (true);
	}
	
	boot::mm::init(addr);

	void (*init)() = (void (*)())0xffff800000000000;
	init();
}
