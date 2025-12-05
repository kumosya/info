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

			boot::printf("Frame Allocator initialized. Metadata at 0x%lx - 0x%lx\n",
				start_addr, pm.start_usable);
			boot::printf("pages start at 0x%lx\n", (uint64_t)pm.pages);

			for (uint64_t i = 0; i < pm.total_pages; ++i) {
				pm.pages[i].flag = PAGE_FREE;
				pm.pages[i].vaddr = pm.start_usable + i * PAGE_SIZE;
				pm.pages[i].count = 0;
			}
			
			
			boot::printf("Total Pages: %lu K, Free Pages: %lu K, Bitmap Size: %lu KiB\n",
				pm.total_pages / 1024, pm.free_pages / 1024, pm.bitmap_size / 1024);
			boot::printf("Usable Memory starts at 0x%lx. reserve_pages=%lu\n", pm.start_usable, reserve_pages);
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
	}

	namespace paging {
		void init() {
			boot::printf("Page init\n");

			// 测试分配几个页框
			int *p = (int *)frame::alloc();
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

			
			pt_entry* pml4 = (pt_entry*)frame::alloc();
			memset(pml4, 0, PAGE_SIZE);
			boot::printf("New PML4 allocated at 0x%lx\n", (uint64_t)pml4);
			mapping_identity(pml4, 0x2000000); // 映射前32MB物理内存
			mapping_kernel(pml4);
			mapping(pml4, 0x6000fffff000, 0x3000000, PTE_PRESENT | PTE_WRITABLE); 
			// 映射虚拟地址0x6000fffff000到物理地址0x3000000，用于测试
			// 这里不知道为什么，像0x8000fffff000这样的地址映射会失败
			// 到时候再修复
			// :(
			// 现在连个异常处理都没有
			// 调试及其麻烦
			// gdb调试内核也不方便
			// 气死了
			
			boot::printf("Identity mapping for first 32MB set up.\n");
			asm __volatile__ (
				"mov %0, %%cr3\n"
				:
				: "r"(pml4)
			);
			boot::printf("Switching to new page table at 0x%lx\n", (uint64_t)pml4);
			// 测试映射是否生效
			int *r = (int *)0x6000fffff000;
			boot::printf("Accessing mapped address 0x3000000: 0x%lx\n", r);
			*r = 0xDEADBEEF;
			boot::printf("Wrote %x to 0x3000000\n", *r);
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
			/* 
			pmd = (pmd_t*) (pud[pud_i] & PAGE_MASK);
			if (!pmd[pmd_i]) {
				pmd[pmd_i] = (pmd_t) boot_mm_page_alloc();
				if (IS_ERR_PTR((void*) pmd[pmd_i])) {
					pmd[pmd_i] = (pmd_t) nullptr;
					return -ENOMEM;
				}

				boot_memset((void*) ((mm::phys_addr_t) pmd[pmd_i]), 0 ,PAGE_SIZE);
				pmd[pmd_i] |= PDE_DEFAULT;
			}

			pte = (pte_t*) (pmd[pmd_i] & PAGE_MASK);
			pte[pte_i] = pa | attr;
	 */
		}

		// 映射内核段
		void mapping_kernel(pt_entry* pml4) {
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
			if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE && mmap->addr >= 0x100000) {
				boot::printf("Available Memory: Addr: 0x%lx, Len: 0x%lx\n", mmap->addr, mmap->len);
				mm::frame::init(mmap->addr + 0x100000, mmap->addr + mmap->len);
			}
			mmap = (multiboot_mmap_entry *)((uint8_t *)mmap + mmap_tag->entry_size);
		}
		paging::init();

		return;
	}
}
