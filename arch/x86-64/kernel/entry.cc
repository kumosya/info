#include <multiboot2.h>
#include <entry.h>

#include <cstdint>

void cppstart (uint32_t magic, uint8_t *addr);
void cppinit(void);

// 外部符号声明
extern char __kernel_start[];

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

	// 输出init()函数的位置
	void (*cppinit_ptr)() = &cppinit;
	boot::printf("init() function address: 0x%lx\n", (uint64_t)cppinit_ptr);
	boot::printf("__kernel_start address: 0x%lx\n", (uint64_t)&__kernel_start);
	while (true);

	// 使用间接跳转调用cppinit函数
	asm volatile (
		"jmp *%0"  // 间接跳转到cppinit函数
		:
		: "r"(cppinit_ptr)
		: "memory"
	);

	/* We should not get here. */
	boot::printf ("Error: cppinit returned.\n");
	while(true);
}
