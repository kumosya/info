#include "page.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "mm.h"
#include "tty.h"

namespace mm::page {
FrameMem frame;
PTE *kernel_pml4;

// Allocate N contiguous pages. Returns physical address (page-aligned) or nullptr.
void *AllocPages(std::size_t n) {
    if (n == 0) n = 1;
    if (frame.free_pages < n) return nullptr;
    std::uint64_t total = frame.total_pages;
    for (std::uint64_t start = 0; start + n <= total; ++start) {
        bool ok = true;
        for (std::size_t j = 0; j < n; ++j) {
            std::uint64_t idx  = start + j;
            std::uint64_t byte = idx / 8;
            int bit       = idx % 8;
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
            int bit       = idx % 8;
            std::uint8_t mask  = (1u << bit);
            frame.bitmap[byte] |= mask;
            frame.pages[idx].flag  = PAGE_USED;
            frame.pages[idx].count = 1;
        }
        frame.free_pages -= n;
        std::uint64_t addr = frame.start_usable + start * PAGE_SIZE;
        return (void *)addr;
    }
    return nullptr;
}

void FreePages(void *addr, std::size_t n) {
    if (!addr || n == 0) return;
    std::uint64_t a = (std::uint64_t)addr;
    if (a < frame.start_usable) return;
    std::uint64_t idx = (a - frame.start_usable) / PAGE_SIZE;
    if (idx >= frame.total_pages) return;
    for (std::size_t i = 0; i < n && (idx + i) < frame.total_pages; ++i) {
        std::uint64_t cur  = idx + i;
        std::uint64_t byte = cur / 8;
        int bit       = cur % 8;
        std::uint8_t mask  = (1u << bit);
        frame.bitmap[byte] &= ~mask;
        frame.pages[cur].flag  = PAGE_FREE;
        frame.pages[cur].count = 0;
    }
    frame.free_pages += n;
}

void Map(PTE *pml4, std::uint64_t virt_addr, std::uint64_t phys_addr, std::uint64_t flags) {
    int pml4_idx = PML4_ENTRY(virt_addr);
    int pdpt_idx = PDPT_ENTRY(virt_addr);
    int pd_idx   = PD_ENTRY(virt_addr);
    int pt_idx   = PT_ENTRY(virt_addr);

    // PML4
    if (!(pml4[pml4_idx].value & PTE_PRESENT)) {
        PTE *pdpt = reinterpret_cast<PTE *>(AllocPages(1));
        memset(pdpt, 0, PAGE_SIZE);
        pml4[pml4_idx].value = ((std::uint64_t)pdpt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }

    PTE *pdpt = reinterpret_cast<PTE *>(pml4[pml4_idx].value & PAGE_MASK);
    // PDPT
    if (!(pdpt[pdpt_idx].value & PTE_PRESENT)) {
        PTE *pd = reinterpret_cast<PTE *>(AllocPages(1));
        memset(pd, 0, PAGE_SIZE);
        pdpt[pdpt_idx].value = ((std::uint64_t)pd & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }
    PTE *pd = reinterpret_cast<PTE *>(pdpt[pdpt_idx].value & PAGE_MASK);

    // PD
    if (!(pd[pd_idx].value & PTE_PRESENT)) {
        PTE *pt = reinterpret_cast<PTE *>(AllocPages(1));
        memset(pt, 0, PAGE_SIZE);
        pd[pd_idx].value = ((std::uint64_t)pt & PAGE_MASK) | PTE_PRESENT | PTE_WRITABLE;
    }
    PTE *pt = reinterpret_cast<PTE *>(pd[pd_idx].value & PAGE_MASK);

    // PT
    pt[pt_idx].value = (phys_addr & PAGE_MASK) | flags;
}

void *Alloc(std::size_t size) {
    if (size == 0) size = 1;
    std::size_t total = size + sizeof(std::uint64_t);
    std::size_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    void *base   = AllocPages(pages);
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
}  // namespace mm::page