#include "multiboot2.h"
#include "page.h"
#include "start.h"

#include <cstdint>

using namespace std;
frame_mem pm;
namespace boot::mm {

void frame_init(uint64_t start_addr, uint64_t end_addr) {
    //			boot::printf("Frame init: 0x%lx - 0x%lx\n", start_addr, end_addr);
    pm.start_addr        = start_addr;
    uint64_t region_size = end_addr - start_addr;
    uint64_t max_pages   = region_size / PAGE_SIZE;

    // 位图和页管理数组需要占用一部分空间，先计算并保留
    pm.bitmap_size            = (max_pages + 7) / 8; // bytes
    uint64_t pages_array_size = max_pages * sizeof(page);
    uint64_t reserve_bytes    = pm.bitmap_size + pages_array_size;
    uint64_t reserve_pages    = (reserve_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    if (reserve_pages >= max_pages) {
        boot::printf("Error: Not enough memory for allocator metadata.\n");
        while (true)
            ;
    }

    uint64_t usable_pages = max_pages - reserve_pages;
    pm.total_pages        = usable_pages;
    pm.free_pages         = usable_pages;

    // bitmap 放在区域起始，pages 数组紧随其后，实际可用物理内存从 reserve_pages * PAGE_SIZE 开始
    pm.bitmap       = (uint8_t *)start_addr;
    pm.pages        = (page *)((uint8_t *)start_addr + pm.bitmap_size);
    pm.start_usable = start_addr + reserve_pages * PAGE_SIZE;

    // 初始化位图和页描述符
    memset(pm.bitmap, 0x00, pm.bitmap_size); // 0 表示空闲
    for (uint64_t i = 0; i < pm.total_pages; ++i) {
        pm.pages[i].flag  = PAGE_FREE;
        pm.pages[i].vaddr = pm.start_usable + i * PAGE_SIZE;
        pm.pages[i].count = 0;
    }
}

void *alloc() {
    if (pm.free_pages == 0) {
        boot::printf("Error: Out of memory!\n");
        while (true)
            ;
    }

    // 查找位图中第一个空闲位
    for (uint64_t byte = 0; byte < pm.bitmap_size; ++byte) {
        if (pm.bitmap[byte] != 0xFF) {
            for (int bit = 0; bit < 8; ++bit) {
                uint64_t idx = byte * 8 + bit;
                if (idx >= pm.total_pages)
                    break;
                uint8_t mask = (1u << bit);
                if (!(pm.bitmap[byte] & mask)) {
                    // 标记为已使用
                    pm.bitmap[byte] |= mask;
                    pm.pages[idx].flag  = PAGE_USED;
                    pm.pages[idx].count = 1;
                    pm.free_pages--;
                    uint64_t addr = pm.start_usable + idx * PAGE_SIZE;
                    return (void *)addr;
                }
            }
        }
    }
    // should not reach here
    boot::printf("Error: allocator failed to find free page although the free_pages > 0.\n");
    while (true)
        ;
}

void mapping(pt_entry *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    int pml4_idx = PML4_ENTRY(virt_addr);
    int pdpt_idx = PDPT_ENTRY(virt_addr);
    int pd_idx   = PD_ENTRY(virt_addr);
    int pt_idx   = PT_ENTRY(virt_addr);

    // PML4
    if (!(pml4[pml4_idx].value & PTE_PRESENT)) {
        pt_entry *pdpt = (pt_entry *)alloc();
        memset(pdpt, 0, PAGE_SIZE);
        pml4[pml4_idx].value = ((uint64_t)pdpt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }

    pt_entry *pdpt = (pt_entry *)(pml4[pml4_idx].value & PAGE_MASK);
    // PDPT
    if (!(pdpt[pdpt_idx].value & PTE_PRESENT)) {
        pt_entry *pd = (pt_entry *)alloc();
        memset(pd, 0, PAGE_SIZE);
        pdpt[pdpt_idx].value = ((uint64_t)pd & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }
    pt_entry *pd = (pt_entry *)(pdpt[pdpt_idx].value & PAGE_MASK);

    // PD
    if (!(pd[pd_idx].value & PTE_PRESENT)) {
        pt_entry *pt = (pt_entry *)alloc();
        memset(pt, 0, PAGE_SIZE);
        pd[pd_idx].value = ((uint64_t)pt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }
    pt_entry *pt = (pt_entry *)(pd[pd_idx].value & PAGE_MASK);

    // PT
    pt[pt_idx].value = (phys_addr & PAGE_MASK) | flags;
}

// 映射内核段
void mapping_kernel(pt_entry *pml4, multiboot_tag_elf_sections *elf_sections) {
    for (size_t i = 0; i < elf_sections->num; i++) {
        char *section   = elf_sections->sections + i * elf_sections->entsize;
        uint64_t vaddr  = *(uint64_t *)(section + 16); // section address
        uint64_t offset = *(uint64_t *)(section + 24); // section offset
        uint64_t size   = *(uint64_t *)(section + 32); // section size
        if (vaddr == (uint64_t)__text_start || vaddr == (uint64_t)__rodata_start || vaddr == (uint64_t)__data_start ||
            vaddr == (uint64_t)__bss_start) {
            //					boot::printf("ELF Section '%d': Vir: 0x%lx Off: 0x%lx Size: %d B\n", i, vaddr, offset, size);
            for (uint64_t addr = 0; addr <= offset; addr += PAGE_SIZE) {
                mapping(pml4, vaddr + addr, 0x100000 - 0x1000 + offset + addr, PTE_PRESENT | PTE_WRITABLE);
            }
        }
    }
}

void mapping_identity(pt_entry *pml4, uint64_t size) {
    for (uint64_t addr = 0; addr < size; addr += PAGE_SIZE) {
        mapping(pml4, addr, addr, PTE_PRESENT | PTE_WRITABLE);
    }
}

void *memset(void *dest, int val, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    while (len-- > 0)
        *ptr++ = val;
    return dest;
}

void init(uint8_t *addr) {
    // boot::printf("MM init\n");

    multiboot_mmap_entry *mmap;
    multiboot_tag *tag                       = (multiboot_tag *)(addr + 8);
    multiboot_tag_mmap *mmap_tag             = NULL;
    multiboot_tag_elf_sections *elf_sections = NULL;
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            mmap_tag = (multiboot_tag_mmap *)tag;
        } else if (tag->type == MULTIBOOT_TAG_TYPE_ELF_SECTIONS) {
            elf_sections = (multiboot_tag_elf_sections *)tag;
        }
        tag = (multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    if (!mmap_tag) {
        boot::printf("Error: Memory map tag is not found!\n");
        while (true)
            ;
    }
    if (!elf_sections) {
        boot::printf("Error: ELF sections tag is not found!\n");
        while (true)
            ;
    }

    mmap               = mmap_tag->entries;
    size_t entry_count = (mmap_tag->size - sizeof(multiboot_tag_mmap)) / mmap_tag->entry_size;
    for (size_t i = 0; i < entry_count; i++) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE && mmap->addr >= 0x100000) {
            //				boot::printf("Available Memory: Addr: 0x%lx, Len: 0x%lx\n", mmap->addr, mmap->len);
            mm::frame_init(mmap->addr + 0x100000, mmap->addr + mmap->len);
            break;
        }
        mmap = (multiboot_mmap_entry *)((uint8_t *)mmap + mmap_tag->entry_size);
    }

    pt_entry *pml4 = (pt_entry *)alloc();
    memset(pml4, 0, PAGE_SIZE);

    mapping_identity(pml4, pm.start_addr + pm.total_pages * PAGE_SIZE);
    mapping_kernel(pml4, elf_sections);

    asm __volatile__("mov %0, %%cr3\n" : : "r"(pml4));
}
} // namespace boot::mm
