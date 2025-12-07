#include <stddef.h>
#include <stdint.h>

#include <entry.h>
#include <page.h>

// Simple kernel operator new/delete backed by page allocator.
// Allocation stores the page count in the first 8 bytes of the allocation.

struct nothrow_t {
	// placement new tag type to suppress exceptions
};


void* operator new(size_t size) noexcept {
    if (size == 0) size = 1;
    size_t total = size + sizeof(uint64_t);
    size_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
    void* base = boot::mm::frame::alloc_pages(pages);
    if (!base) {
        // allocation failure: halt
        while (1) asm volatile("hlt");
    }
    // store page count at base
    *((uint64_t*)base) = pages;
    return (void*)((uint8_t*)base + sizeof(uint64_t));
}

void operator delete(void* ptr) noexcept {
    if (!ptr) return;
    uint8_t* p = (uint8_t*)ptr;
    uint8_t* header = p - sizeof(uint64_t);
    uint64_t base = (uint64_t)header & PAGE_MASK;
    uint64_t pages = *((uint64_t*)base);
    boot::mm::frame::free_pages((void*)base, pages);
}

void* operator new[](size_t size) noexcept { return operator new(size); }
void operator delete[](void* ptr) noexcept { operator delete(ptr); }

// nothrow variants
void* operator new(size_t size, const nothrow_t&) noexcept { return operator new(size); }
void operator delete(void* ptr, const nothrow_t&) noexcept { operator delete(ptr); }
void* operator new[](size_t size, const nothrow_t&) noexcept { return operator new(size); }
void operator delete[](void* ptr, const nothrow_t&) noexcept { operator delete(ptr); }

// sized delete (C++14+)
void operator delete(void* ptr, size_t size) noexcept { operator delete(ptr); }
void operator delete[](void* ptr, size_t size) noexcept { operator delete(ptr); }
