#include "page.h"
#include "mm.h"
#include "tty.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Forward-declare the boot allocator functions (implemented in kernel/mm.cc)
namespace boot {
namespace mm {
namespace frame {
void *alloc_pages(size_t n);
void free_pages(void *addr, size_t n);
} // namespace frame
} // namespace mm
} // namespace boot

namespace mm::page {
void *alloc(size_t size) {
    if (size == 0)
        size = 1;
    size_t total = size + sizeof(uint64_t);
    size_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    void *base   = boot::mm::frame::alloc_pages(pages);
    if (!base) {
        // allocation failure: halt
        return NULL;
    }
    // store page count at base
    *((uint64_t *)base) = pages;
    // Delegate to the boot frame allocator which manages physical pages.
    return boot::mm::frame::alloc_pages(pages);
}

void free(void *addr) {
    if (!addr)
        return;
    uint8_t *p      = (uint8_t *)addr;
    uint8_t *header = p - sizeof(uint64_t);
    uint64_t base   = (uint64_t)header & PAGE_MASK;
    uint64_t pages  = *((uint64_t *)base);
    // Delegate to the boot frame allocator
    boot::mm::frame::free_pages(addr, pages);
}
} // namespace mm::page