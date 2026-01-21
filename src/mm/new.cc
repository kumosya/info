#include <cstdint>

#include "kernel/mm.h"
#include "kernel/page.h"

// Simple kernel operator new/delete backed by page allocator.
// Allocation stores the page count in the first 8 bytes of the allocation.

typedef unsigned long size_t;

struct nothrow_t {
    // placement new tag type to suppress exceptions
};

void *operator new(size_t size) noexcept { return mm::page::Alloc(size); }

void operator delete(void *ptr) noexcept { mm::page::Free(ptr); }

void *operator new[](size_t size) noexcept { return mm::page::Alloc(size); }
void operator delete[](void *ptr) noexcept { mm::page::Free(ptr); }

// nothrow variants
void *operator new(size_t size, const nothrow_t &) noexcept {
    return mm::page::Alloc(size);
}
void operator delete(void *ptr, const nothrow_t &) noexcept {
    mm::page::Free(ptr);
}
void *operator new[](size_t size, const nothrow_t &) noexcept {
    return mm::page::Alloc(size);
}
void operator delete[](void *ptr, const nothrow_t &) noexcept {
    mm::page::Free(ptr);
}

// sized delete (C++14+)
void operator delete(void *ptr, size_t) noexcept {
    mm::page::Free(ptr);
}
void operator delete[](void *ptr, size_t) noexcept {
    mm::page::Free(ptr);
}
