#include <cstdint>
#include <cstdlib>

typedef unsigned long size_t;

struct nothrow_t {
    // placement new tag type to suppress exceptions
};

void *operator new(size_t size) noexcept { return malloc(size); }

void operator delete(void *ptr) noexcept { free(ptr); }

void *operator new[](size_t size) noexcept { return malloc(size); }
void operator delete[](void *ptr) noexcept { free(ptr); }

// nothrow variants
void *operator new(size_t size, const nothrow_t &) noexcept {
    return malloc(size);
}
void operator delete(void *ptr, const nothrow_t &) noexcept {
    free(ptr);
}
void *operator new[](size_t size, const nothrow_t &) noexcept {
    return malloc(size);
}
void operator delete[](void *ptr, const nothrow_t &) noexcept {
    free(ptr);
}

void operator delete(void *ptr, size_t) noexcept { free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { free(ptr); }
