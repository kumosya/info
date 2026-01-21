
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "kernel/page.h"
#include "kernel/mm.h"
#include "kernel/tty.h"

namespace mm::page {
FrameMem frame;
PTE *kernel_pml4;

// Allocate N contiguous pages. Returns physical address (page-aligned) or
// nullptr.
void *AllocPages(std::size_t n) {
    if (n == 0) n = 1;
    if (frame.free_pages < n) return nullptr;
    std::uint64_t total = frame.total_pages;
    for (std::uint64_t start = 0; start + n <= total; ++start) {
        bool ok = true;
        for (std::size_t j = 0; j < n; ++j) {
            std::uint64_t idx  = start + j;
            std::uint64_t byte = idx / 8;
            int bit            = idx % 8;
            std::uint8_t mask  = (1u << bit);
            if (frame.bitmap[byte] & mask) {
                ok = false;
                start += j;
                break;
            }
        }
        if (!ok) continue;
        // mark bits
        for (std::size_t j = 0; j < n; ++j) {
            std::uint64_t idx  = start + j;
            std::uint64_t byte = idx / 8;
            int bit            = idx % 8;
            std::uint8_t mask  = (1u << bit);
            frame.bitmap[byte] |= mask;
            frame.pages[idx].flag  = PAGE_USED;
            frame.pages[idx].count = 1;
        }
        frame.free_pages -= n;
        std::uint64_t addr = frame.start_usable + start * PAGE_SIZE;
        return (void *)Phy2Vir(addr);
    }
    return nullptr;
}

void FreePages(void *addr, std::size_t n) {
    if (!addr || n == 0) return;
    std::uint64_t a = (std::uint64_t)(std::uint64_t)addr;
    if (a < frame.start_usable) return;
    std::uint64_t idx = (a - frame.start_usable) / PAGE_SIZE;
    if (idx >= frame.total_pages) return;
    for (std::size_t i = 0; i < n && (idx + i) < frame.total_pages; ++i) {
        std::uint64_t cur  = idx + i;
        std::uint64_t byte = cur / 8;
        int bit            = cur % 8;
        std::uint8_t mask  = (1u << bit);
        frame.bitmap[byte] &= ~mask;
        frame.pages[cur].flag  = PAGE_FREE;
        frame.pages[cur].count = 0;
    }
    frame.free_pages += n;
}

void Map(PTE *pml4, std::uint64_t virt_addr, std::uint64_t phys_addr,
         std::uint64_t flags) {
    int pml4_idx = PML4_ENTRY(virt_addr);
    int pdpt_idx = PDPT_ENTRY(virt_addr);
    int pd_idx   = PD_ENTRY(virt_addr);
    int pt_idx   = PT_ENTRY(virt_addr);
    // PML4 -> PDPT
    if (!(pml4[pml4_idx].value & PTE_PRESENT)) {
        PTE *pdpt = reinterpret_cast<PTE *>(AllocPages(1));
        memset(pdpt, 0, PAGE_SIZE);
        pml4[pml4_idx].value =
            ((std::uint64_t)Vir2Phy((std::uint64_t)pdpt) & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE | flags;
    }
    PTE *pdpt = reinterpret_cast<PTE *>(pml4[pml4_idx].value & PAGE_MASK);
    if ((std::uint64_t)pdpt < 0xffff800000000000ULL) {
        pdpt = (PTE *)Phy2Vir((std::uint64_t)pdpt);
    }

    // PDPT -> PD
    if (!(pdpt[pdpt_idx].value & PTE_PRESENT)) {
        PTE *pd = reinterpret_cast<PTE *>(AllocPages(1));
        memset(pd, 0, PAGE_SIZE);
        pdpt[pdpt_idx].value =
            ((std::uint64_t)Vir2Phy((std::uint64_t)pd) & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE | flags;
    }
    PTE *pd = reinterpret_cast<PTE *>((std::uint64_t)pdpt[pdpt_idx].value & PAGE_MASK);
    if ((std::uint64_t)pd < 0xffff800000000000ULL) {
        pd = (PTE *)Phy2Vir((std::uint64_t)pd);
    }

    // PD -> PT
    if (!(pd[pd_idx].value & PTE_PRESENT)) {
        PTE *pt = reinterpret_cast<PTE *>(AllocPages(1));
        memset(pt, 0, PAGE_SIZE);
        pd[pd_idx].value =
            ((std::uint64_t)Vir2Phy((std::uint64_t)pt) & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE | flags;
    }
    PTE *pt = reinterpret_cast<PTE *>((std::uint64_t)pd[pd_idx].value & PAGE_MASK);
    if ((std::uint64_t)pt < 0xffff800000000000ULL) {
        pt = (PTE *)Phy2Vir((std::uint64_t)pt);
    }

    // PT
    pt[pt_idx].value = (phys_addr & PAGE_MASK) | flags;
}

void *Alloc(std::size_t size) {
    if (size == 0) size = 1;
    std::size_t total = size + sizeof(std::uint64_t);
    std::size_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    void *base        = AllocPages(pages);
    if (!base) {
        // allocation failure: halt
        return NULL;
    }
    // store page count at base
    *(reinterpret_cast<std::uint64_t *>(base)) = pages;

    return base;
}

void Free(void *addr) {
    if (!addr) return;
    std::uint8_t *p      = reinterpret_cast<std::uint8_t *>(addr);
    std::uint8_t *header = p - sizeof(std::uint64_t);
    std::uint64_t base   = (std::uint64_t)header & PAGE_MASK;
    std::uint64_t pages  = *(reinterpret_cast<std::uint64_t *>(base));
    // Delegate to the boot frame allocator
    FreePages(addr, pages);
}

void UpdateKernelPml4(PTE *user_pml4) {
    // Ensure the provided user PML4 contains the kernel (higher-half)
    // entries by copying them from the canonical kernel_pml4.
    // This preserves kernel mappings when switching to a user page table.
    memcpy(&user_pml4[256], &mm::page::kernel_pml4[256], 256 * sizeof(PTE));
}

/**
 * Helper: Parse PTE flags to a char buffer (no std::string)
 * @param pte_value PTE raw value
 * @param out_buf Output buffer (must be large enough, e.g., 256 bytes)
 * @param buf_size Size of output buffer
 */
static void ParsePTEFlags(std::uint64_t pte_value, char* out_buf, size_t buf_size) {
    memset(out_buf, 0, buf_size); // Clear buffer first
    
    if (pte_value & PTE_PRESENT)    strncat(out_buf, "P(Present) ", buf_size - strlen(out_buf) - 1);
    if (pte_value & PTE_WRITABLE)   strncat(out_buf, "W(Writable) ", buf_size - strlen(out_buf) - 1);
    if (pte_value & PTE_USER)       strncat(out_buf, "U(User) ", buf_size - strlen(out_buf) - 1);
    if (pte_value & PTE_WRITE_THROUGH)        strncat(out_buf, "PWT(Write-Through) ", buf_size - strlen(out_buf) - 1);
    if (pte_value & PTE_CACHE_DISABLE)        strncat(out_buf, "PCD(Cache-Disable) ", buf_size - strlen(out_buf) - 1);
    if (pte_value & PTE_ACCESSED)   strncat(out_buf, "A(Accessed) ", buf_size - strlen(out_buf) - 1);
    if (pte_value & PTE_DIRTY)      strncat(out_buf, "D(Dirty) ", buf_size - strlen(out_buf) - 1);
    if (pte_value & PTE_GLOBAL)     strncat(out_buf, "G(Global) ", buf_size - strlen(out_buf) - 1);
    if (pte_value & PTE_NO_EXECUTE)         strncat(out_buf, "NX(No-Execute) ", buf_size - strlen(out_buf) - 1);

    // If no flags, set default message
    if (strlen(out_buf) == 0) {
        strncpy(out_buf, "No valid flags", buf_size - 1);
    }
}

/**
 * Core Function: Analyze page table mapping for a virtual address
 * @param pml4 Virtual address of PML4 table
 * @param virt_addr Virtual address to analyze (no need for page alignment)
 * @return Physical address (0 = no mapping)
 */
std::uint64_t AnalyzePageTable(PTE* pml4, std::uint64_t virt_addr) {
    // Align virtual address to page boundary
    std::uint64_t va_aligned = virt_addr & PAGE_MASK;
    tty::printk("== Page Table Analysis: Virtual Address 0x%lx (Aligned: 0x%lx) ==\n", 
           virt_addr, va_aligned);

    // Parse indices for each page table level
    int pml4_idx = PML4_ENTRY(va_aligned);
    int pdpt_idx = PDPT_ENTRY(va_aligned);
    int pd_idx   = PD_ENTRY(va_aligned);
    int pt_idx   = PT_ENTRY(va_aligned);
    tty::printk("Index: PML4=%d, PDPT=%d, PD=%d, PT=%d\n", 
           pml4_idx, pdpt_idx, pd_idx, pt_idx);

    // -------------------------- Analyze PML4 Level --------------------------
    PTE& pml4_entry = pml4[pml4_idx];
    char pml4_flags[256] = {0};
    ParsePTEFlags(pml4_entry.value, pml4_flags, sizeof(pml4_flags));
    
    tty::printk("  PML4 Entry Physical Address: 0x%lx\n", (std::uint64_t)&pml4_entry);
    tty::printk("       Entry Value: 0x%lx\n", pml4_entry.value);
    tty::printk("       Flags: %s\n", pml4_flags);
    
    if (!(pml4_entry.value & PTE_PRESENT)) {
        tty::printk(" [x] PML4 Entry not present - No mapping!\n");
        return 0;
    }

    // -------------------------- Analyze PDPT Level --------------------------
    std::uint64_t pdpt_phys = pml4_entry.value & PAGE_MASK;
    PTE* pdpt = reinterpret_cast<PTE*>(pdpt_phys);
    if (pdpt_phys < 0xffff800000000000ULL) {
        pdpt = reinterpret_cast<PTE*>(mm::Phy2Vir(pdpt_phys));
    }
    
    char pdpt_flags[256] = {0};
    PTE& pdpt_entry = pdpt[pdpt_idx];
    ParsePTEFlags(pdpt_entry.value, pdpt_flags, sizeof(pdpt_flags));
    
    tty::printk("  PDPT Table Physical Base: 0x%lx\n", pdpt_phys);
    tty::printk("       Table Virtual Address: 0x%lx\n", (std::uint64_t)pdpt);
    tty::printk("       Entry Value: 0x%lx\n", pdpt_entry.value);
    tty::printk("       Flags: %s\n", pdpt_flags);
    
    if (!(pdpt_entry.value & PTE_PRESENT)) {
        tty::printk(" [x] PDPT Entry not present - No mapping!\n");
        return 0;
    }

    // -------------------------- Analyze PD Level --------------------------
    std::uint64_t pd_phys = pdpt_entry.value & PAGE_MASK;
    PTE* pd = reinterpret_cast<PTE*>(pd_phys);
    if (pd_phys < 0xffff800000000000ULL) {
        pd = reinterpret_cast<PTE*>(mm::Phy2Vir(pd_phys));
    }
    
    char pd_flags[256] = {0};
    PTE& pd_entry = pd[pd_idx];
    ParsePTEFlags(pd_entry.value, pd_flags, sizeof(pd_flags));
    
    tty::printk("  PD Table Physical Base: 0x%lx\n", pd_phys);
    tty::printk("     Table Virtual Address: 0x%lx\n", (std::uint64_t)pd);
    tty::printk("     Entry Value: 0x%lx\n", pd_entry.value);
    tty::printk("     Flags: %s\n", pd_flags);
    
    if (!(pd_entry.value & PTE_PRESENT)) {
        tty::printk(" [x] PD Entry not present - No mapping!\n");
        return 0;
    }

    // -------------------------- Analyze PT Level --------------------------
    std::uint64_t pt_phys = pd_entry.value & PAGE_MASK;
    PTE* pt = reinterpret_cast<PTE*>(pt_phys);
    if (pt_phys < 0xffff800000000000ULL) {
        pt = reinterpret_cast<PTE*>(mm::Phy2Vir(pt_phys));
    }
    
    char pt_flags[256] = {0};
    PTE& pt_entry = pt[pt_idx];
    ParsePTEFlags(pt_entry.value, pt_flags, sizeof(pt_flags));
    
    tty::printk("  PT Table Physical Base: 0x%lx\n", pt_phys);
    tty::printk("     Table Virtual Address: 0x%lx\n", (std::uint64_t)pt);
    tty::printk("     Entry Value: 0x%lx\n", pt_entry.value);
    tty::printk("     Flags: %s\n", pt_flags);
    
    if (!(pt_entry.value & PTE_PRESENT)) {
        tty::printk(" [x] PT Entry not present - No mapping!\n");
        return 0;
    }

    // -------------------------- Calculate Physical Address --------------------------
    std::uint64_t phy_page_base = pt_entry.value & PAGE_MASK;
    std::uint64_t page_offset = virt_addr & ~PAGE_MASK;
    std::uint64_t phy_addr = phy_page_base + page_offset;
    
    tty::printk("\nMapping Successful!\n");
    tty::printk("  Virtual Address 0x%lx → Physical Address 0x%lx\n", virt_addr, phy_addr);
    tty::printk("  Physical Page Base: 0x%lx, Page Offset: 0x%lX\n", phy_page_base, page_offset);

    return phy_addr;
}


}  // namespace mm::page