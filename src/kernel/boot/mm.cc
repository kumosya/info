#include <cstdint>

#include "kernel/multiboot2.h"
#include "kernel/page.h"
#include "kernel/start.h"

FrameMem pm;

namespace boot::mm {

static struct {
    std::uint32_t num;
    std::uint32_t entsize;
    std::uint32_t shndx;
    char sections[4096];
} saved_elf;

static std::uint64_t Vir2Phy(std::uint64_t virt) {
    return virt - IDENTITY_BASE;
}

static std::uint64_t Phy2Vir(std::uint64_t phy) { return phy + IDENTITY_BASE; }

void *memcpy(void *dest, const void *src, size_t len) {
    std::uint8_t *d = (std::uint8_t *)dest;
    const std::uint8_t *s = (const std::uint8_t *)src;
    while (len-- > 0) *d++ = *s++;
    return dest;
}

void frameInit(std::uint64_t start_addr, std::uint64_t end_addr) {
    //			boot::printf("Frame init: 0x%lx - 0x%lx\n", start_addr,
    // end_addr);
    pm.start_addr             = start_addr;
    std::uint64_t region_size = end_addr - start_addr;
    std::uint64_t max_pages   = region_size / PAGE_SIZE;

    // 位图和页管理数组需要占用一部分空间，先计算并保留
    pm.bitmap_size                 = (max_pages + 7) / 8;  // bytes
    std::uint64_t pages_array_size = max_pages * sizeof(Page);
    std::uint64_t reserve_bytes    = pm.bitmap_size + pages_array_size;
    std::uint64_t reserve_pages = (reserve_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    if (reserve_pages >= max_pages) {
        boot::printf("Error: Not enough memory for allocator metadata.\n");
        while (true);
    }

    std::uint64_t usable_pages = max_pages - reserve_pages;
    pm.total_pages             = usable_pages;
    pm.free_pages              = usable_pages;

    // bitmap 放在区域起始，pages 数组紧随其后，实际可用物理内存从 reserve_pages
    // * PAGE_SIZE 开始
    pm.bitmap       = (std::uint8_t *)start_addr;
    pm.pages        = (Page *)((std::uint8_t *)start_addr + pm.bitmap_size);
    pm.start_usable = start_addr + reserve_pages * PAGE_SIZE;

    // 初始化位图和页描述符
    memset(pm.bitmap, 0x00, pm.bitmap_size);  // 0 表示空闲
    for (std::uint64_t i = 0; i < pm.total_pages; ++i) {
        pm.pages[i].flag  = PAGE_FREE;
        pm.pages[i].vaddr = pm.start_usable + i * PAGE_SIZE;
        pm.pages[i].count = 0;
    }
}

void *Alloc() {
    if (pm.free_pages == 0) {
        boot::printf("Error: Out of memory!\n");
        while (true);
    }

    // 查找位图中第一个空闲位
    for (std::uint64_t byte = 0; byte < pm.bitmap_size; ++byte) {
        if (pm.bitmap[byte] != 0xFF) {
            for (int bit = 0; bit < 8; ++bit) {
                std::uint64_t idx = byte * 8 + bit;
                if (idx >= pm.total_pages) break;
                std::uint8_t mask = (1u << bit);
                if (!(pm.bitmap[byte] & mask)) {
                    // 标记为已使用
                    pm.bitmap[byte] |= mask;
                    pm.pages[idx].flag  = PAGE_USED;
                    pm.pages[idx].count = 1;
                    pm.free_pages--;
                    std::uint64_t addr = pm.start_usable + idx * PAGE_SIZE;
                    return (void *)addr;
                }
            }
        }
    }
    // should not reach here
    boot::printf(
        "Error: allocator failed to find free page although the free_pages > "
        "0.\n");
    while (true);
}

void mapping(PTE *pml4, std::uint64_t virt_addr, std::uint64_t phys_addr,
             std::uint64_t flags) {
    int pml4_idx = PML4_ENTRY(virt_addr);
    int pdpt_idx = PDPT_ENTRY(virt_addr);
    int pd_idx   = PD_ENTRY(virt_addr);
    int pt_idx   = PT_ENTRY(virt_addr);

    // PML4
    if (!(pml4[pml4_idx].value & PTE_PRESENT)) {
        PTE *pdpt = (PTE *)Alloc();
        memset(pdpt, 0, PAGE_SIZE);
        pml4[pml4_idx].value = ((std::uint64_t)pdpt & PAGE_MASK) | PTE_PRESENT |
                               PTE_WRITABLE | flags;
    }

    PTE *pdpt = (PTE *)(pml4[pml4_idx].value & PAGE_MASK);
    // PDPT
    if (!(pdpt[pdpt_idx].value & PTE_PRESENT)) {
        PTE *pd = (PTE *)Alloc();
        memset(pd, 0, PAGE_SIZE);
        pdpt[pdpt_idx].value = ((std::uint64_t)pd & PAGE_MASK) | PTE_PRESENT |
                               PTE_WRITABLE | flags;
    }
    PTE *pd = (PTE *)(pdpt[pdpt_idx].value & PAGE_MASK);

    // PD
    if (!(pd[pd_idx].value & PTE_PRESENT)) {
        PTE *pt = (PTE *)Alloc();
        memset(pt, 0, PAGE_SIZE);
        pd[pd_idx].value = ((std::uint64_t)pt & PAGE_MASK) | PTE_PRESENT |
                           PTE_WRITABLE | flags;
    }
    PTE *pt = (PTE *)(pd[pd_idx].value & PAGE_MASK);

    // PT
    pt[pt_idx].value = (phys_addr & PAGE_MASK) | flags;
}

// 映射内核段
void MappingKernel(PTE *pml4) {
    boot::printf("Mapping Kernel... %d sections, entsize=%d\n", saved_elf.num, saved_elf.entsize);
    
    for (std::uint32_t i = 0; i < saved_elf.num; i++) {
        char *section = saved_elf.sections + i * saved_elf.entsize;
        
        std::uint64_t vaddr = *(std::uint64_t *)(section + 16);
        std::uint64_t offset = *(std::uint64_t *)(section + 24);
        std::uint64_t size = *(std::uint64_t *)(section + 32);
        
        boot::printf("section[%d]: addr=0x%lx off=0x%lx size=0x%lx\n", i, vaddr, offset, size);
        
        if (vaddr == reinterpret_cast<std::uint64_t>(__text_start)) {
            for (std::uint64_t addr = 0; addr <= __text_end - __text_start;
                 addr += PAGE_SIZE) {
                mapping(pml4, vaddr + addr, 0x100000 - 0x1000 + offset + addr,
                        PTE_PRESENT | PTE_WRITABLE);
            }
        } else if (vaddr == reinterpret_cast<std::uint64_t>(__rodata_start)) {
            for (std::uint64_t addr = 0; addr <= __rodata_end - __rodata_start;
                 addr += PAGE_SIZE) {
                mapping(pml4, vaddr + addr, 0x100000 - 0x1000 + offset + addr,
                        PTE_PRESENT | PTE_WRITABLE);
            }
        } else if (vaddr == reinterpret_cast<std::uint64_t>(__data_start)) {
            for (std::uint64_t addr = 0; addr <= __data_end - __data_start;
                 addr += PAGE_SIZE) {
                mapping(pml4, vaddr + addr, 0x100000 - 0x1000 + offset + addr,
                        PTE_PRESENT | PTE_WRITABLE);
            }
        } else if (vaddr == reinterpret_cast<std::uint64_t>(__bss_start)) {
            for (std::uint64_t addr = 0;
                 addr <= __bss_end - __bss_start + 0x20000; addr += PAGE_SIZE) {
                mapping(pml4, vaddr + addr, 0x100000 - 0x1000 + offset + addr,
                        PTE_PRESENT | PTE_WRITABLE);
            }
        }
    }
}

void MappingIdentity(PTE *pml4, std::uint64_t size) {
    for (std::uint64_t addr = 0; addr < 0xf00000; addr += PAGE_SIZE) {
        mapping(pml4, addr, addr, PTE_PRESENT | PTE_WRITABLE);
    }
    for (std::uint64_t addr = 0; addr < size && size < 0xfffffffffff;
         addr += PAGE_SIZE) {
        mapping(pml4, IDENTITY_BASE + addr, addr, PTE_PRESENT | PTE_WRITABLE);
    }
}

void *memset(void *dest, int val, size_t len) {
    std::uint8_t *ptr = (std::uint8_t *)dest;
    while (len-- > 0) *ptr++ = val;
    return dest;
}

void Init(std::uint8_t *addr) {
    std::uint64_t mbi_start = (std::uint64_t)addr;
    std::uint64_t mbi_size = *(std::uint32_t *)addr;
    std::uint64_t mbi_end = mbi_start + mbi_size;
    
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
        tag = (multiboot_tag *)((std::uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    if (!mmap_tag) {
        boot::printf("Error: Memory map tag is not found!\n");
        while (true);
    }
    if (!elf_sections) {
        boot::printf("Error: ELF sections tag is not found!\n");
        while (true);
    }
    
    std::uint8_t *elf_raw = (std::uint8_t *)elf_sections;
    saved_elf.num = *(std::uint32_t *)(elf_raw + 8);
    saved_elf.entsize = *(std::uint32_t *)(elf_raw + 12);
    saved_elf.shndx = *(std::uint32_t *)(elf_raw + 16);
    std::uint64_t sections_size = saved_elf.num * saved_elf.entsize;
    if (sections_size > 4096) sections_size = 4096;
    memcpy(saved_elf.sections, elf_raw + 20, sections_size);

    mmap = mmap_tag->entries;
    size_t entry_count =
        (mmap_tag->size - sizeof(multiboot_tag_mmap)) / mmap_tag->entry_size;
    for (size_t i = 0; i < entry_count; i++) {
        if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE &&
            mmap->addr >= 0x100000) {
            std::uint64_t mem_start = mmap->addr + 0x100000;
            std::uint64_t mem_end = mmap->addr + mmap->len;
            
            if (mem_start < mbi_end) {
                mem_start = (mbi_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            }
            
            boot::printf("Frame init: 0x%lx - 0x%lx (skipped mbi at 0x%lx-0x%lx)\n", 
                mem_start, mem_end, mbi_start, mbi_end);
            
            mm::frameInit(mem_start, mem_end);
            break;
        }
        mmap = (multiboot_mmap_entry *)((std::uint8_t *)mmap +
                                        mmap_tag->entry_size);
    }

    PTE *pml4 = (PTE *)Alloc();
    memset(pml4, 0, PAGE_SIZE);

    MappingIdentity(pml4, pm.start_addr + pm.total_pages * PAGE_SIZE);
    MappingKernel(pml4);

    asm __volatile__("mov %0, %%cr3\n" : : "r"(pml4));
}
}  // namespace boot::mm
