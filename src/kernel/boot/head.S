 /* clang-format off */
#define ASM_FILE        1
#include "multiboot2.h"
#include "page.h"
#include "cpu.h"

/*  C++ symbol format. HAVE_ASM_USCORE is defined by configure. */
#ifdef HAVE_ASM_USCORE
# define EXT_CPP(sym)                     _ ## sym
#else
# define EXT_CPP(sym)                     sym
#endif

/*  The size of our stack (16KB). */
#define STACK_SIZE                      0x4000
        
.section .boot.header
        
multiboot_header:
	/*  Align 64 bits boundary. */
	.align  8
	/*  magic */
	.long   MULTIBOOT2_HEADER_MAGIC
	/*  ISA: i386 */
	.long   MULTIBOOT_ARCHITECTURE_I386
	/*  Header length. */
	.long   multiboot_header_end - multiboot_header
	/*  checksum */
	.long   -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT_ARCHITECTURE_I386 + (multiboot_header_end - multiboot_header))
entry_address_tag_start:        
	.align  8
	.short MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS_EFI64
	.short MULTIBOOT_HEADER_TAG_OPTIONAL
	.long entry_address_tag_end - entry_address_tag_start
	/*  entry_addr */
	.long _start
entry_address_tag_end:

tag_frame_buffer:
	.align 8
	.short MULTIBOOT_HEADER_TAG_FRAMEBUFFER
	.short 0
	.long 20
	.long 1024
	.long 768
	.long 32

end_tag_start:
	.align  8
	.short MULTIBOOT_HEADER_TAG_END
	.short 0
	.long end_tag_end - end_tag_start
end_tag_end:
multiboot_header_end:

.section .boot.text
    .code32
    .align 0x1000
    .extern CppStart
    .globl _start

_start:
	mov     $boot_stack_top, %esp

	/* Set the boot information as parameters for CppStart() */
	mov     %eax, %edi
	mov     %ebx, %esi

	/* Clear eflags */
	cli
	
	/* Disable paging (UEFI may turn it on) */
	mov     %cr0, %eax
	mov     $CR0_PG, %ebx
	not     %ebx
	and     %ebx, %eax
	mov     %eax, %cr0

	/* Set up page table for booting stage */
	mov     $pt, %eax
	or      $(PTE_PRESENT | PTE_WRITABLE), %eax
	mov     %eax, pd

	xor     %eax, %eax
	or      $(PTE_PRESENT | PTE_WRITABLE | PTE_PAGE_SIZE), %eax
	movl    %eax, pt

	xor     %eax, %eax
	mov     %eax, (pd + 4)
	mov     %eax, (pt + 4)

	/* Load page table */
	mov     $pd, %eax
	mov     %eax, %cr3

	/* Enable PAE and PGE */
	mov     %cr4, %eax
	or      $(CR4_PAE | CR4_PGE), %eax
	mov     %eax, %cr4

	/* Enabling EFER.LME */
	mov     $0xC0000080, %ecx
	rdmsr
	or      $(1 << 8), %eax
	wrmsr

	/* Enable paging */
	mov     %cr0, %eax
	or      $CR0_PG, %eax
	mov     %eax, %cr0

	/* Set up GDT */
	mov     $gdt64_ptr, %eax
	lgdt    0(%eax)

	/* Reload all the segment registers */
	mov $(2 << SELECTOR_INDEX), %ax
	mov     %ax, %ds
	mov     %ax, %ss
	mov     %ax, %es
	mov     %ax, %fs
	mov     %ax, %gs

	/* Enter 64-bit world */
	jmp $(1 << SELECTOR_INDEX), $CppStart

	/* We shouldn't get here... */
.loop:
	hlt
	jmp .loop

.section .boot.data
    .align 0x1000

    .globl boot_stack, boot_stack_top, pd, pt

boot_stack:
	.space 0x1000
boot_stack_top:

pd:
	.space 0x1000
pt:
	.space 0x1000

/* 64 bits GDT */
.align 0x1000	/* it should be aligned to page */
.globl gdt64, gdt64_ptr
gdt64:
	.quad 0 /* first one must be zero */
gdt64_code_segment:
	.quad 0x00209A0000000000 /* exec/read */
gdt64_data_segment:
	.quad 0x0000920000000000 /* read/write */
gdt64_tss_lo:
    .quad 0
gdt64_tss_hi:
    .quad 0
gdt64_ptr:
	.short gdt64_ptr - gdt64 - 1    /* GDT limit */
	.long gdt64                     /* GDT Addr */

/* 64-bit TSS area: reserved and filled at runtime */
.align 16
tss64:
    .space 104
