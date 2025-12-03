#include <entry.h>
#include <multiboot2.h>
#include <page.h>

#include <stdint.h>

namespace boot::mm {
	frame::mem pm;
	namespace frame {
		void init(uint64_t start_addr, uint64_t end_addr) {
			boot::printf("Frame init: 0x%lx - 0x%lx\n", start_addr, end_addr);
			pm.start_addr = start_addr;
			pm.total_pages = (end_addr - start_addr) / PAGE_SIZE;
			pm.bitmap_size = (pm.total_pages + 7) / 8;
			pm.free_pages = pm.total_pages;
			pm.bitmap = (uint8_t*)start_addr;
			memset(pm.bitmap, 0xFF, pm.bitmap_size);

			boot::printf("Total Pages: %lu K, Free Pages: %lu K, Bitmap Size: %lu KiB\n",
						pm.total_pages / 1024, pm.free_pages / 1024, pm.bitmap_size / 1024);
		}

		void* alloc() {
			if (pm.free_pages == 0) {
				boot::printf("Error: Out of memory!\n");
				while (true);
			}
			// 简单起见，直接返回下一个可用页框的地址
			uint64_t addr = pm.start_addr + (pm.total_pages - pm.free_pages) * PAGE_SIZE;
			pm.free_pages--;
			return (void*)addr;
		}

		void free(void* addr) {
			memset(addr, 0, PAGE_SIZE);
			pm.free_pages++;
		}
	}

	namespace page {
		void init() {
			boot::printf("Page init\n");
			uint64_t* pml4 = (uint64_t*)frame::alloc();
			memset(pml4, 0, PAGE_SIZE);

			mapping_kernel(pml4);
			//mapping_identity(pml4, 0x100000); // 映射前1MiB物理内存
			boot::printf("Switching to new page table at 0x%lx\n", (uint64_t)pml4);
			while (true) ;
			asm volatile (
				"mov %0, %%cr3\n"
				:
				: "r"(pml4)
			);
		}

		void mapping(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
			int pml4_idx = PML4_ENTRY(virt_addr);
			int pdpt_idx = PDPT_ENTRY(virt_addr);
			int pd_idx = PD_ENTRY(virt_addr);
			int pt_idx = PT_ENTRY(virt_addr);
			
			// PML4
			if (!(pml4[pml4_idx] & PTE_PRESENT)) {
				uint64_t* pdpt = (uint64_t*)frame::alloc();
				memset(pdpt, 0, PAGE_SIZE);
				pml4[pml4_idx] = ((uint64_t)pdpt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
			}
			uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & PAGE_MASK);
			// PDPT
			if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
				uint64_t* pd = (uint64_t*)frame::alloc();
				memset(pd, 0, PAGE_SIZE);
				pdpt[pdpt_idx] = ((uint64_t)pd & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
			}
			uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & PAGE_MASK);

			// PD
			if (!(pd[pd_idx] & PTE_PRESENT)) {
				uint64_t* pt = (uint64_t*)frame::alloc();
				memset(pt, 0, PAGE_SIZE);
				pd[pd_idx] = ((uint64_t)pt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
			}
			uint64_t* pt = (uint64_t*)(pd[pd_idx] & PAGE_MASK);

			// PT
			pt = (uint64_t*) (pd[pd_idx] & PAGE_MASK);
			pt[pt_idx] = phys_addr | flags;
		}

		// 映射内核段
		void mapping_kernel(uint64_t* pml4) {
			const uint64_t KERNEL_VIRT_BASE = 0xffff800000000000;
			const uint64_t KERNEL_PHYS_BASE = 0x100000;
			const uint64_t KERNEL_MAPPING_SIZE = 1 * 1024 * 1024; // 3 MiB
		}

		void mapping_identity(uint64_t* pml4, uint64_t size) {
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


	void init(uint8_t *addr) {
		boot::printf("MM init\n");

		multiboot_mmap_entry *mmap;
		multiboot_tag *tag = (multiboot_tag *)(addr + 8);;
		while (tag->type != MULTIBOOT_TAG_TYPE_MMAP) {
			if (tag->type == MULTIBOOT_TAG_TYPE_END) {
				boot::printf("Error: Memory map tag is not found!\n");
				while (true);
			}
			tag = (multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
		}
		
		multiboot_tag_mmap *mmap_tag = (multiboot_tag_mmap *)tag;
		mmap = mmap_tag->entries;
		size_t entry_count = (mmap_tag->size - sizeof(multiboot_tag_mmap)) / mmap_tag->entry_size;
		for (size_t i = 0; i < entry_count; i++) {
			if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
//				boot::printf("Available Memory: Addr: 0x%lx, Len: 0x%lx\n", mmap->addr, mmap->len);
				mm::frame::init(mmap->addr, mmap->addr + mmap->len);
			}
			mmap = (multiboot_mmap_entry *)((uint8_t *)mmap + mmap_tag->entry_size);
		}

		page::init();

		return;
	}
}
