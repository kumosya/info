#include <entry.h>
#include <multiboot2.h>
#include <page.h>
#include <idt.h>

#include <stdint.h>

/* 这个内存管理其实还是有一些问题的，不过就先这样了 */

extern char __kernel_start[];
extern char __kernel_end[];

extern char __text_start[];
extern char __rodata_start[];
extern char __data_start[];
extern char __bss_start[];

extern void cppinit();

static idt_entry idt_ety[256];

static void set_idt_entry(int vec, void* handler, uint16_t sel, uint8_t type_attr) {
	uint64_t addr = (uint64_t)handler;
	idt_ety[vec].offset_low = addr & 0xFFFF;
	idt_ety[vec].selector = sel;
	idt_ety[vec].ist = 0;
	idt_ety[vec].type_attr = type_attr;
	idt_ety[vec].offset_mid = (addr >> 16) & 0xFFFF;
	idt_ety[vec].offset_high = (addr >> 32) & 0xFFFFFFFF;
	idt_ety[vec].zero = 0;
}

extern "C" void ge_fault_handler_c() {
	boot::printf("#GP Fault!\n");
	// Halt so user can inspect
	while (true) { asm volatile("hlt"); }
}
// C handler called from the assembly stub. Prints CR2 and halts.
extern "C" void page_fault_handler_c(uint64_t fault_addr) {
	boot::printf("Page Fault! CR2=0x%lx\n", fault_addr);
	// Halt so user can inspect
	while (true) { asm volatile("hlt"); }
}

namespace boot::mm {
	frame_mem pm;

	namespace frame {
		void init(uint64_t start_addr, uint64_t end_addr) {
//			boot::printf("Frame init: 0x%lx - 0x%lx\n", start_addr, end_addr);
			pm.start_addr = start_addr;
			uint64_t region_size = end_addr - start_addr;
			uint64_t max_pages = region_size / PAGE_SIZE;

			// 位图和页管理数组需要占用一部分空间，先计算并保留
			pm.bitmap_size = (max_pages + 7) / 8; // bytes
			uint64_t pages_array_size = max_pages * sizeof(page);
			uint64_t reserve_bytes = pm.bitmap_size + pages_array_size;
			uint64_t reserve_pages = (reserve_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

			if (reserve_pages >= max_pages) {
				boot::printf("Error: Not enough memory for allocator metadata.\n");
				while (true);
			}
			
			uint64_t usable_pages = max_pages - reserve_pages;
			pm.total_pages = usable_pages;
			pm.free_pages = usable_pages;

			// bitmap 放在区域起始，pages 数组紧随其后，实际可用物理内存从 reserve_pages * PAGE_SIZE 开始
			pm.bitmap = (uint8_t*)start_addr;
			pm.pages = (page*)((uint8_t*)start_addr + pm.bitmap_size);
			pm.start_usable = start_addr + reserve_pages * PAGE_SIZE;
			
			// 初始化位图和页描述符
			memset(pm.bitmap, 0x00, pm.bitmap_size); // 0 表示空闲

/*			boot::printf("Frame Allocator initialized. Metadata at 0x%lx - 0x%lx\n",
				start_addr, pm.start_usable);
			boot::printf("pages start at 0x%lx\n", (uint64_t)pm.pages);
*/
			for (uint64_t i = 0; i < pm.total_pages; ++i) {
				pm.pages[i].flag = PAGE_FREE;
				pm.pages[i].vaddr = pm.start_usable + i * PAGE_SIZE;
				pm.pages[i].count = 0;
			}
			
			
/*			boot::printf("Total Pages: %lu K, Free Pages: %lu K, Bitmap Size: %lu KiB\n",
				pm.total_pages / 1024, pm.free_pages / 1024, pm.bitmap_size / 1024);
			boot::printf("Usable Memory starts at 0x%lx. reserve_pages=%lu\n", pm.start_usable, reserve_pages);
*/
			}

		void* alloc() {
			if (pm.free_pages == 0) {
				boot::printf("Error: Out of memory!\n");
				while (true);
			}

			// 查找位图中第一个空闲位
			for (uint64_t byte = 0; byte < pm.bitmap_size; ++byte) {
				if (pm.bitmap[byte] != 0xFF) {
					for (int bit = 0; bit < 8; ++bit) {
						uint64_t idx = byte * 8 + bit;
						if (idx >= pm.total_pages) break;
						uint8_t mask = (1u << bit);
						if (!(pm.bitmap[byte] & mask)) {
							// 标记为已使用
							pm.bitmap[byte] |= mask;
							pm.pages[idx].flag = PAGE_USED;
							pm.pages[idx].count = 1;
							pm.free_pages--;
							uint64_t addr = pm.start_usable + idx * PAGE_SIZE;
							return (void*)addr;
						}
					}
				}
			}
			// should not reach here
			boot::printf("Error: allocator failed to find free page despite free_pages > 0\n");
			while (true);
		}

		// Allocate N contiguous pages. Returns physical address (page-aligned) or nullptr.
		void* alloc_pages(size_t n) {
			if (n == 0) n = 1;
			if (pm.free_pages < n) return nullptr;
			uint64_t total = pm.total_pages;
			for (uint64_t start = 0; start + n <= total; ++start) {
				bool ok = true;
				for (uint64_t j = 0; j < n; ++j) {
					uint64_t idx = start + j;
					uint64_t byte = idx / 8;
					int bit = idx % 8;
					uint8_t mask = (1u << bit);
					if (pm.bitmap[byte] & mask) { ok = false; start += j; break; }
				}
				if (!ok) continue;
				// mark bits
				for (uint64_t j = 0; j < n; ++j) {
					uint64_t idx = start + j;
					uint64_t byte = idx / 8;
					int bit = idx % 8;
					uint8_t mask = (1u << bit);
					pm.bitmap[byte] |= mask;
					pm.pages[idx].flag = PAGE_USED;
					pm.pages[idx].count = 1;
				}
				pm.free_pages -= n;
				uint64_t addr = pm.start_usable + start * PAGE_SIZE;
				return (void*)addr;
			}
			return nullptr;
		}

		void free(void* addr) {
			uint64_t a = (uint64_t)addr;
			if (a < pm.start_usable) return;
			uint64_t idx = (a - pm.start_usable) / PAGE_SIZE;
			if (idx >= pm.total_pages) return;
			uint64_t byte = idx / 8;
			int bit = idx % 8;
			uint8_t mask = (1u << bit);
			pm.bitmap[byte] &= ~mask;
			pm.pages[idx].flag = PAGE_FREE;
			pm.pages[idx].count = 0;
			memset(addr, 0, PAGE_SIZE);
			pm.free_pages++;
		}

		
		void free_pages(void* addr, size_t n) {
			if (!addr || n == 0) return;
			uint64_t a = (uint64_t)addr;
			if (a < pm.start_usable) return;
			uint64_t idx = (a - pm.start_usable) / PAGE_SIZE;
			if (idx >= pm.total_pages) return;
			for (size_t i = 0; i < n && (idx + i) < pm.total_pages; ++i) {
				uint64_t cur = idx + i;
				uint64_t byte = cur / 8;
				int bit = cur % 8;
				uint8_t mask = (1u << bit);
				pm.bitmap[byte] &= ~mask;
				pm.pages[cur].flag = PAGE_FREE;
				pm.pages[cur].count = 0;
			}
			pm.free_pages += n;
		}
	}

	namespace paging {
		void init(multiboot_tag_elf_sections *elf_sections) {
//			boot::printf("Page init\n");

			// 测试分配几个页框
/*			int *p = (int *)frame::alloc();
			boot::printf("Test alloc page1 at 0x%lx\n", (uint64_t)p);

			*p = 0x12345678;
			boot::printf("Value at allocated page: 0x%x\n", *p);

			frame::free(p);
			boot::printf("Freed page at 0x%lx\n", (uint64_t)p);
			int *q = (int *)frame::alloc();
			boot::printf("Test alloc page2 at 0x%lx\n", (uint64_t)q);

			q[0] = 0x87654321;
			q[1] = 0xABCDEF00;
			boot::printf("Values at allocated page2: 0x%x 0x%x\n", q[0], q[1]);
			frame::free(q);
			boot::printf("Freed page at 0x%lx\n\n", (uint64_t)q);

*/			
			pt_entry* pml4 = (pt_entry*)frame::alloc();
			memset(pml4, 0, PAGE_SIZE);
			//boot::printf("New PML4 allocated at 0x%lx\n", (uint64_t)pml4);
			
			mapping_identity(pml4, 0x2000000); // 映射前32MB物理内存
			mapping_kernel(pml4, elf_sections);
			
			//boot::printf("cppinit: 0x%lx\n", (uint64_t)cppinit);
			//mapping(pml4, 0xffff8dddfffff000, 0x3000000, PTE_PRESENT | PTE_WRITABLE); 
			
			//boot::printf("Identity mapping for first 32MB set up.\n");
			asm __volatile__ (
				"mov %0, %%cr3\n"
				:
				: "r"(pml4)
			);
			//boot::printf("Switching to new page table at 0x%lx\n", (uint64_t)pml4);
			// 测试映射是否生效
			/*int *r = (int *)0xffff8dddfffff000;
			boot::printf("Accessing mapped address 0x3000000: 0x%lx\n", r);
			*r = 0xDEADBEEF;
			boot::printf("Wrote %x to 0x3000000\n", *r);
			boot::printf("cppinit: 0x%lx, ", (uint64_t)cppinit);
			*/
		}

		void mapping(pt_entry* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
			int pml4_idx = PML4_ENTRY(virt_addr);
			int pdpt_idx = PDPT_ENTRY(virt_addr);
			int pd_idx = PD_ENTRY(virt_addr);
			int pt_idx = PT_ENTRY(virt_addr);
			
			// PML4
			if (!(pml4[pml4_idx].value & PTE_PRESENT)) {
				pt_entry* pdpt = (pt_entry*)frame::alloc();
				memset(pdpt, 0, PAGE_SIZE);
				pml4[pml4_idx].value = ((uint64_t)pdpt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
			}

			pt_entry* pdpt = (pt_entry*)(pml4[pml4_idx].value & PAGE_MASK);
			// PDPT
			if (!(pdpt[pdpt_idx].value & PTE_PRESENT)) {
				pt_entry* pd = (pt_entry*)frame::alloc();
				memset(pd, 0, PAGE_SIZE);
				pdpt[pdpt_idx].value = ((uint64_t)pd & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
			}
			pt_entry* pd = (pt_entry*)(pdpt[pdpt_idx].value & PAGE_MASK);

			// PD
			if (!(pd[pd_idx].value & PTE_PRESENT)) {
				pt_entry* pt = (pt_entry*)frame::alloc();
				memset(pt, 0, PAGE_SIZE);
				pd[pd_idx].value = ((uint64_t)pt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
			}
			pt_entry* pt = (pt_entry*)(pd[pd_idx].value & PAGE_MASK);

			// PT
			pt[pt_idx].value = (phys_addr & PAGE_MASK) | flags;
		}


		// 映射内核段
		void mapping_kernel(pt_entry* pml4, multiboot_tag_elf_sections *elf_sections) {
			for (size_t i = 0; i < elf_sections->num; i++) {
				char *section = elf_sections->sections + i * elf_sections->entsize;
				uint64_t vaddr = *(uint64_t *)(section + 16); // section address
				uint64_t offset = *(uint64_t *)(section + 24); // section offset
				uint64_t size = *(uint64_t *)(section + 32); // section size
				if (vaddr == (uint64_t)__text_start || vaddr == (uint64_t)__rodata_start ||
					vaddr == (uint64_t)__data_start) {
//					boot::printf("ELF Section '%d': Vir: 0x%lx Off: 0x%lx Size: %d B\n", i, vaddr, offset, size);
					for (uint64_t addr = 0; addr < offset; addr += PAGE_SIZE) {
						mapping(pml4, vaddr + addr, 0x100000 - 0x1000 + offset + addr, PTE_PRESENT | PTE_WRITABLE);
					}
				}
			}
		}

		void mapping_identity(pt_entry* pml4, uint64_t size) {
			for (uint64_t addr = 0; addr < size; addr += PAGE_SIZE) {
				mapping(pml4, addr, addr, PTE_PRESENT | PTE_WRITABLE);
			}
		}
	}

	static void *memset (void *dest, int val, size_t len)
	{
		uint8_t *ptr = (uint8_t *)dest;
		while (len-- > 0)	*ptr++ = val;
		return dest;
	}

	void ge_fault_stub(void) {
		asm __volatile__ (
		"    cli\n"
		"    /* call the C handler; it will not return */\n"
		"    call ge_fault_handler_c\n"
		"1:\t hlt\n"
		"    jmp 1b\n");
	}
	void page_fault_stub(void) {
		asm __volatile__ (
		"    cli\n"
		"    /* read CR2 into RDI (first arg) */\n"
		"    mov %cr2, %rdi\n"
		"    /* call the C handler; it will not return */\n"
		"    call page_fault_handler_c\n"
		"1:\t hlt\n"
		"    jmp 1b\n");
	}
	
	void init(uint8_t *addr) {
		//boot::printf("MM init\n");
		
		multiboot_mmap_entry *mmap;
		multiboot_tag *tag = (multiboot_tag *)(addr + 8);
		multiboot_tag_mmap *mmap_tag = NULL;
		multiboot_tag_elf_sections *elf_sections = NULL;
		while (tag->type != MULTIBOOT_TAG_TYPE_END) {
			if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
				mmap_tag = (multiboot_tag_mmap *)tag;
			}
			else if (tag->type == MULTIBOOT_TAG_TYPE_ELF_SECTIONS) {
				elf_sections = (multiboot_tag_elf_sections *)tag;
			}
			tag = (multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
		}

		if (!mmap_tag) {
			boot::printf("Error: Memory map tag is not found!\n");
			while (true);
		}
		if (!elf_sections) {
			boot::printf("Error: ELF sections tag is not found!\n");
			while (true);
		}
		
		mmap = mmap_tag->entries;
		size_t entry_count = (mmap_tag->size - sizeof(multiboot_tag_mmap)) / mmap_tag->entry_size;
		for (size_t i = 0; i < entry_count; i++) {
			if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE && mmap->addr >= 0x100000) {
//				boot::printf("Available Memory: Addr: 0x%lx, Len: 0x%lx\n", mmap->addr, mmap->len);
				mm::frame::init(mmap->addr + 0x100000, mmap->addr + mmap->len);
			}
			mmap = (multiboot_mmap_entry *)((uint8_t *)mmap + mmap_tag->entry_size);
		}

		// Install minimal IDT and page-fault handler before enabling mappings
		// zero out idt
		memset(&idt_ety, 0, sizeof(idt_ety));
		// kernel code selector is 0x08 (see gdt setup in boot.S)
//		boot::printf("handler set for page fault at 0x%lx\n", (uint64_t)page_fault_stub);
		set_idt_entry(13, (void*)ge_fault_stub, 0x08, 0x8E);
		set_idt_entry(14, (void*)page_fault_stub, 0x08, 0x8E);
		idt_ptr_t idtp;
		idtp.limit = sizeof(idt_ety) - 1;
		idtp.base = (uint64_t)&idt_ety;
		asm __volatile__ ("lidt %0" : : "m" (idtp));
//		boot::printf("IDT loaded, page-fault handler installed.\n");

		paging::init(elf_sections);

		cppinit();
	}
}
