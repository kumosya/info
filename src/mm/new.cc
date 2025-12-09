#include "mm.h"
#include "page.h"
#include <cstddef>
#include <cstdint>

using namespace std;

// Simple kernel operator new/delete backed by page allocator.
// Allocation stores the page count in the first 8 bytes of the allocation.

struct nothrow_t {
    // placement new tag type to suppress exceptions
};

void *operator new(size_t size) noexcept { return mm::page::alloc(size); }

void operator delete(void *ptr) noexcept { mm::page::free(ptr); }

void *operator new[](size_t size) noexcept { return operator new(size); }
void operator delete[](void *ptr) noexcept { operator delete(ptr); }

// nothrow variants
void *operator new(size_t size, const nothrow_t &) noexcept { return operator new(size); }
void operator delete(void *ptr, const nothrow_t &) noexcept { operator delete(ptr); }
void *operator new[](size_t size, const nothrow_t &) noexcept { return operator new(size); }
void operator delete[](void *ptr, const nothrow_t &) noexcept { operator delete(ptr); }

// sized delete (C++14+)
void operator delete(void *ptr, size_t size) noexcept { operator delete(ptr); }
void operator delete[](void *ptr, size_t size) noexcept { operator delete(ptr); }
