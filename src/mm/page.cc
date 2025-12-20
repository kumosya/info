#include "page.h"
#include "mm.h"
#include "tty.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mm::page {
frame_mem frame;
pt_entry *kernel_pml4;

// Allocate N contiguous pages. Returns physical address (page-aligned) or nullptr.
void *pages_alloc(size_t n) {
    if (n == 0)
        n = 1;
    if (frame.free_pages < n)
        return nullptr;
    uint64_t total = frame.total_pages;
    for (uint64_t start = 0; start + n <= total; ++start) {
        bool ok = true;
        for (uint64_t j = 0; j < n; ++j) {
            uint64_t idx  = start + j;
            uint64_t byte = idx / 8;
            int bit       = idx % 8;
            uint8_t mask  = (1u << bit);
            if (frame.bitmap[byte] & mask) {
                ok = false;
                start += j;
                break;
            }
        }
        if (!ok)
            continue;
        // mark bits
        for (uint64_t j = 0; j < n; ++j) {
            uint64_t idx  = start + j;
            uint64_t byte = idx / 8;
            int bit       = idx % 8;
            uint8_t mask  = (1u << bit);
            frame.bitmap[byte] |= mask;
            frame.pages[idx].flag  = PAGE_USED;
            frame.pages[idx].count = 1;
        }
        frame.free_pages -= n;
        uint64_t addr = frame.start_usable + start * PAGE_SIZE;
        return (void *)addr;
    }
    return nullptr;
}

void pages_free(void *addr, size_t n) {
    if (!addr || n == 0)
        return;
    uint64_t a = (uint64_t)addr;
    if (a < frame.start_usable)
        return;
    uint64_t idx = (a - frame.start_usable) / PAGE_SIZE;
    if (idx >= frame.total_pages)
        return;
    for (size_t i = 0; i < n && (idx + i) < frame.total_pages; ++i) {
        uint64_t cur  = idx + i;
        uint64_t byte = cur / 8;
        int bit       = cur % 8;
        uint8_t mask  = (1u << bit);
        frame.bitmap[byte] &= ~mask;
        frame.pages[cur].flag  = PAGE_FREE;
        frame.pages[cur].count = 0;
    }
    frame.free_pages += n;
}

void map(pt_entry *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    int pml4_idx = PML4_ENTRY(virt_addr);
    int pdpt_idx = PDPT_ENTRY(virt_addr);
    int pd_idx   = PD_ENTRY(virt_addr);
    int pt_idx   = PT_ENTRY(virt_addr);

    // PML4
    if (!(pml4[pml4_idx].value & PTE_PRESENT)) {
        pt_entry *pdpt = (pt_entry *)alloc(sizeof(pt_entry));
        memset(pdpt, 0, PAGE_SIZE);
        pml4[pml4_idx].value = ((uint64_t)pdpt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }

    pt_entry *pdpt = (pt_entry *)(pml4[pml4_idx].value & PAGE_MASK);
    // PDPT
    if (!(pdpt[pdpt_idx].value & PTE_PRESENT)) {
        pt_entry *pd = (pt_entry *)alloc(sizeof(pt_entry));
        memset(pd, 0, PAGE_SIZE);
        pdpt[pdpt_idx].value = ((uint64_t)pd & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }
    pt_entry *pd = (pt_entry *)(pdpt[pdpt_idx].value & PAGE_MASK);

    // PD
    if (!(pd[pd_idx].value & PTE_PRESENT)) {
        pt_entry *pt = (pt_entry *)alloc(sizeof(pt_entry));
        memset(pt, 0, PAGE_SIZE);
        pd[pd_idx].value = ((uint64_t)pt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }
    pt_entry *pt = (pt_entry *)(pd[pd_idx].value & PAGE_MASK);

    // PT
    pt[pt_idx].value = (phys_addr & PAGE_MASK) | flags;
}

void *alloc(size_t size) {
    if (size == 0)
        size = 1;
    size_t total = size + sizeof(uint64_t);
    size_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    void *base   = pages_alloc(pages);
    if (!base) {
        // allocation failure: halt
        return NULL;
    }
    // store page count at base
    *((uint64_t *)base) = pages;
    
    return base;
}

void free(void *addr) {
    if (!addr)
        return;
    uint8_t *p      = (uint8_t *)addr;
    uint8_t *header = p - sizeof(uint64_t);
    uint64_t base   = (uint64_t)header & PAGE_MASK;
    uint64_t pages  = *((uint64_t *)base);
    // Delegate to the boot frame allocator
    pages_free(addr, pages);
}
} // namespace mm::page